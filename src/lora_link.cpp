/*
 * lora_link.cpp — see lora_link.h. Radio configuration is a line-for-line port of the config
 * bench-proven on the same RAK3112/RAK3312 module (siot-lorawan-node, ESP-IDF, RadioLib 7.7.1).
 */
#include "lora_link.h"

#if BOARD_HAS_LORA

#include "log.h"
#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#include <string.h>

// credentials.h provides LORA_DEV_EUI / LORA_JOIN_EUI / LORA_APP_KEY (see credentials.h.example)
#include "credentials.h"

#ifndef LORA_DEV_EUI
#define LORA_DEV_EUI  0x0000000000000000ULL
#endif
#ifndef LORA_JOIN_EUI
#define LORA_JOIN_EUI 0x0000000000000000ULL
#endif
#ifndef LORA_APP_KEY
#define LORA_APP_KEY  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

namespace {

enum CmdType : uint8_t { CMD_SCAN = 0, CMD_HEARTBEAT, CMD_JOIN, CMD_CLEAR_SESSION };

struct Cmd {
    uint8_t type;
    uint8_t len;
    uint8_t payload[LORA_PAYLOAD_MAX];
};

// SX1262 on the global SPI object (= FSPI/SPI2 on the ESP32-S3). TFT_eSPI owns SPI3.
SX1262      radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, SPI);
LoRaWANNode node(&radio, &AS923, 0);   // AS923-1 (frequency offset 0)

Preferences    prefs;                  // namespace "lorawan": nonces, session — task-only
QueueHandle_t  cmdQueue  = nullptr;
TaskHandle_t   taskHandle = nullptr;
portMUX_TYPE   mux = portMUX_INITIALIZER_UNLOCKED;

LoraStatus  status  = {};              // shared, guarded by mux
LoraAppInfo appInfo = {};              // shared, guarded by mux

uint64_t devEui  = 0;
uint64_t joinEui = 0;
uint8_t  appKey[16] = LORA_APP_KEY;

// task-local
uint32_t lastUplinkMs    = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t backoffUntilMs  = 0;
uint8_t  backoffStep     = 0;
uint32_t joinFailures    = 0;
uint8_t  consecutiveFail = 0;
bool     heartbeatPending = false;
bool     joinedOnce      = false;

void setState(LoraState s) {
    portENTER_CRITICAL(&mux);
    status.state = s;
    portEXIT_CRITICAL(&mux);
}

LoraState getState() {
    portENTER_CRITICAL(&mux);
    LoraState s = status.state;
    portEXIT_CRITICAL(&mux);
    return s;
}

uint32_t epochNow() {
    const time_t t = time(nullptr);
    return (t > 1600000000) ? (uint32_t)t : 0u;   // before 2020-09 = never synced
}

bool credsPresent() {
    for (size_t i = 0; i < sizeof(appKey); i++) {
        if (appKey[i] != 0u) return true;
    }
    return false;
}

uint64_t devEuiFromMac() {
    // EUI-48 -> EUI-64 by inserting FF:FE. The base MAC equals the STA MAC the station claims with.
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    return ((uint64_t)mac[0] << 56) | ((uint64_t)mac[1] << 48) | ((uint64_t)mac[2] << 40) |
           ((uint64_t)0xFF << 32) | ((uint64_t)0xFE << 24) |
           ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
}

bool nvsLoad(const char* key, uint8_t* buf, size_t len) {
    return prefs.getBytesLength(key) == len && prefs.getBytes(key, buf, len) == len;
}

void nvsSaveNonces() {
    prefs.putBytes("nonces", node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

void nvsSaveSession() {
    prefs.putBytes("session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

bool doJoin() {
    portENTER_CRITICAL(&mux);
    status.joinAttempts++;
    portEXIT_CRITICAL(&mux);

    int16_t st = node.beginOTAA(joinEui, devEui, appKey, appKey);
    if (st != RADIOLIB_ERR_NONE) {
        LOG_E("[LoRa] beginOTAA failed (%d)\n", st);
        return false;
    }

    // Restore DevNonce/JoinNonce and any prior session: DevNonce must climb monotonically
    // (ChirpStack rejects reuse) and a restored session skips the join entirely.
    uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    if (nvsLoad("nonces", nonces, sizeof(nonces))) {
        node.setBufferNonces(nonces);
    }
    uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    if (nvsLoad("session", session, sizeof(session))) {
        node.setBufferSession(session);
    }

    st = node.activateOTAA();
    nvsSaveNonces();   // after EVERY attempt: DevNonce was incremented even on failure

    if (st == RADIOLIB_LORAWAN_NEW_SESSION || st == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        nvsSaveSession();
        // AS923 DR3 = SF9BW125: dwell-legal and short enough for the TxDone wait (SF12 hits -5
        // TX_TIMEOUT). ADR off for determinism.
        node.setDatarate(3);
        node.setADR(false);
        portENTER_CRITICAL(&mux);
        status.lastRc = st;
        status.fcntUp = node.getFCntUp();
        portEXIT_CRITICAL(&mux);
        LOG_I("[LoRa] %s; uplink DR3 (SF9)\n",
              st == RADIOLIB_LORAWAN_NEW_SESSION ? "JOINED AS923 (new session)" : "session restored (no re-join)");
        return true;
    }
    portENTER_CRITICAL(&mux);
    status.lastRc = st;
    portEXIT_CRITICAL(&mux);
    LOG_E("[LoRa] join failed (%d) — DevNonce persisted, next attempt uses a higher nonce\n", st);
    return false;
}

void clearSessionNvs() {
    node.clearSession();
    prefs.remove("nonces");
    prefs.remove("session");
    LOG_W("[LoRa] session + nonces cleared; re-joining\n");
}

int16_t sendUplink(const uint8_t* data, size_t len, uint8_t fPort) {
    // Spacing: our own minimum gap and whatever the stack requires (dwell / duty limits).
    const uint32_t now = millis();
    uint32_t wait = 0;
    if (lastUplinkMs != 0u) {
        const uint32_t since = now - lastUplinkMs;
        if (since < LORA_MIN_UPLINK_GAP_MS) wait = LORA_MIN_UPLINK_GAP_MS - since;
    }
    const RadioLibTime_t stackWait = node.timeUntilUplink();
    if (stackWait > wait) wait = (uint32_t)stackWait;
    if (wait > 0u) {
        vTaskDelay(pdMS_TO_TICKS(wait));
    }

    if (len > node.getMaxPayloadLen()) {
        LOG_E("[LoRa] payload %u > max %u at current DR\n", (unsigned)len, (unsigned)node.getMaxPayloadLen());
        return RADIOLIB_ERR_PACKET_TOO_LONG;
    }

    const int16_t rc = node.sendReceive(data, len, fPort, false);
    lastUplinkMs = millis();
    nvsSaveSession();   // frame counters survive reboots (replay protection)

    portENTER_CRITICAL(&mux);
    status.lastRc = rc;
    status.lastUplinkMs = lastUplinkMs;
    status.fcntUp = node.getFCntUp();
    if (rc < RADIOLIB_ERR_NONE) status.uplinksFail++; else status.uplinksOk++;
    portEXIT_CRITICAL(&mux);

    if (rc < RADIOLIB_ERR_NONE) {
        consecutiveFail++;
        LOG_E("[LoRa] uplink failed (%d) fPort=%u\n", rc, fPort);
    } else {
        consecutiveFail = 0;
        LOG_I("[LoRa] uplink OK fPort=%u len=%u fcnt=%lu rx=%d\n", fPort, (unsigned)len,
              (unsigned long)node.getFCntUp(), rc);
    }
    return rc;
}

void sendHeartbeat(uint8_t reason) {
    LoraAppInfo info;
    uint32_t dropped;
    portENTER_CRITICAL(&mux);
    info = appInfo;
    dropped = status.dropped;
    portEXIT_CRITICAL(&mux);

    lora_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    hb.uptime_s           = millis() / 1000u;
    hb.queued_http_events = info.queuedHttpEvents;
    hb.last_seq           = info.lastSeq;
    hb.lora_dropped       = dropped > 255u ? 255u : (uint8_t)dropped;
    lora_fw_version_bytes(FW_VERSION, hb.fw);
    hb.wifi_rssi          = info.wifiRssi;
    hb.offline_reason     = reason;
    hb.epoch              = epochNow();
    hb.flags              = (info.operatorLoggedIn ? LORA_HB_FLAG_OPERATOR_LOGGED_IN : 0u) |
                            (info.stationMapped    ? LORA_HB_FLAG_STATION_MAPPED     : 0u) |
                            (info.wifiAssociated   ? LORA_HB_FLAG_WIFI_ASSOCIATED    : 0u);

    uint8_t buf[LORA_HEARTBEAT_LEN];
    const size_t n = lora_encode_heartbeat(&hb, buf, sizeof(buf));
    if (n > 0u) {
        sendUplink(buf, n, LORA_FPORT_HEARTBEAT);
    }
    lastHeartbeatMs = millis();
}

void enqueue(const Cmd& c) {
    if (cmdQueue == nullptr) return;
    if (xQueueSend(cmdQueue, &c, 0) != pdTRUE) {
        // Full: drop the oldest LoRa copy. The NVS HTTP queue is the durable store.
        Cmd victim;
        if (xQueueReceive(cmdQueue, &victim, 0) == pdTRUE) {
            portENTER_CRITICAL(&mux);
            status.dropped++;
            portEXIT_CRITICAL(&mux);
        }
        xQueueSend(cmdQueue, &c, 0);
    }
}

void handleCmd(const Cmd& c, uint8_t offlineReason) {
    switch (c.type) {
        case CMD_SCAN:          sendUplink(c.payload, c.len, LORA_FPORT_SCAN); break;
        case CMD_HEARTBEAT:     sendHeartbeat(offlineReason); break;
        case CMD_JOIN:          setState(LORA_JOINING); break;
        case CMD_CLEAR_SESSION: clearSessionNvs(); setState(LORA_JOINING); break;
        default: break;
    }
}

void loraTask(void*) {
    prefs.begin("lorawan", false);

    if (!credsPresent()) {
        setState(LORA_NO_CREDS);
        LOG_W("[LoRa] AppKey is all zero — LoRaWAN disabled (fill LORA_APP_KEY in credentials.h)\n");
        for (;;) {
            Cmd c;
            if (xQueueReceive(cmdQueue, &c, portMAX_DELAY) == pdTRUE && c.type == CMD_SCAN) {
                portENTER_CRITICAL(&mux);
                status.dropped++;
                portEXIT_CRITICAL(&mux);
            }
        }
    }

    LOG_I("[LoRa] OTAA DevEUI=%016llX JoinEUI=%016llX (creds: compiled, DevEUI: %s)\n",
          (unsigned long long)devEui, (unsigned long long)joinEui, LORA_DEV_EUI != 0ULL ? "compiled" : "MAC");

    for (;;) {
        portENTER_CRITICAL(&mux);
        status.taskStackFree = uxTaskGetStackHighWaterMark(nullptr);
        status.queued = (uint8_t)uxQueueMessagesWaiting(cmdQueue);
        const bool linkDown = status.linkDown;
        const uint8_t reason = status.offlineReason;
        portEXIT_CRITICAL(&mux);

        const LoraState st = getState();

        if (st == LORA_JOINING) {
            if (doJoin()) {
                setState(LORA_JOINED);
                backoffStep = 0;
                consecutiveFail = 0;
                if (!joinedOnce) {
                    joinedOnce = true;
                    heartbeatPending = true;   // one frame so the device shows "last seen" in ChirpStack
                }
            } else {
                joinFailures++;
                uint32_t backoff = (backoffStep == 0) ? 10000u : (backoffStep == 1) ? 30000u : 60000u;
                if (joinFailures >= 10u) backoff = 300000u;   // spare the channel and the server
                if (backoffStep < 2) backoffStep++;
                backoffUntilMs = millis() + backoff;
                setState(LORA_BACKOFF);
                LOG_W("[LoRa] next join attempt in %lu s\n", (unsigned long)(backoff / 1000u));
            }
            continue;
        }

        if (st == LORA_BACKOFF) {
            const int32_t remaining = (int32_t)(backoffUntilMs - millis());
            if (remaining <= 0) {
                setState(LORA_JOINING);
                continue;
            }
            Cmd c;
            if (xQueuePeek(cmdQueue, &c, pdMS_TO_TICKS((uint32_t)remaining)) == pdTRUE) {
                if (c.type == CMD_JOIN || c.type == CMD_CLEAR_SESSION) {
                    xQueueReceive(cmdQueue, &c, 0);
                    if (c.type == CMD_CLEAR_SESSION) clearSessionNvs();
                    setState(LORA_JOINING);
                } else {
                    // data commands need a session; sleep out the rest of the backoff
                    const int32_t left = (int32_t)(backoffUntilMs - millis());
                    if (left > 0) vTaskDelay(pdMS_TO_TICKS((uint32_t)left));
                }
            }
            continue;
        }

        // LORA_JOINED
        uint32_t waitMs = 60000u;   // re-evaluate periodically even when idle
        if (heartbeatPending) {
            waitMs = 0u;
        } else if (linkDown) {
            const int32_t due = (int32_t)((lastHeartbeatMs + LORA_HEARTBEAT_INTERVAL_MS) - millis());
            waitMs = due <= 0 ? 0u : (uint32_t)due;
        }

        Cmd c;
        if (xQueueReceive(cmdQueue, &c, pdMS_TO_TICKS(waitMs)) == pdTRUE) {
            handleCmd(c, linkDown ? reason : LORA_OFFLINE_NONE);
        } else if (heartbeatPending) {
            heartbeatPending = false;
            sendHeartbeat(linkDown ? reason : LORA_OFFLINE_NONE);
        } else if (linkDown && (int32_t)((lastHeartbeatMs + LORA_HEARTBEAT_INTERVAL_MS) - millis()) <= 0) {
            sendHeartbeat(reason);
        }

        int16_t lastRc;
        portENTER_CRITICAL(&mux);
        lastRc = status.lastRc;
        portEXIT_CRITICAL(&mux);
        if (getState() == LORA_JOINED && (consecutiveFail >= 5u || lastRc == RADIOLIB_ERR_NETWORK_NOT_JOINED)) {
            LOG_W("[LoRa] %s — re-joining\n", consecutiveFail >= 5u ? "5 consecutive uplink failures" : "network says not joined");
            consecutiveFail = 0;
            setState(LORA_JOINING);
        }
    }
}

uint8_t eventTypeCode(const String& eventType) {
    if (eventType == "COMPLETE") return LORA_EVT_COMPLETE;
    if (eventType == "QC_PASS")  return LORA_EVT_QC_PASS;
    if (eventType == "QC_FAIL")  return LORA_EVT_QC_FAIL;
    return 0u;
}

} // namespace

bool loraInit() {
    // MUST precede radio.begin(): RadioLib's Arduino HAL calls SPI.begin() without pins, and
    // SPIClass::begin() is a no-op once the bus is up. Without this the FSPI default pins
    // (GPIO10-13 = the LCD bus) would be claimed. ss = -1: RadioLib drives NSS itself.
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, -1);

    // TCXO 1.8 V via begin (8th arg), DC-DC regulator (useRegulatorLDO = false); retry at 1.6 V.
    float tcxo = 1.8f;
    int16_t st = radio.begin(923.2, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, tcxo, false);
    if (st != RADIOLIB_ERR_NONE) {
        LOG_W("[LoRa] radio.begin @1.8V failed (%d); retrying @1.6V\n", st);
        tcxo = 1.6f;
        st = radio.begin(923.2, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, tcxo, false);
    }
    if (st != RADIOLIB_ERR_NONE) {
        LOG_E("[LoRa] radio.begin failed (%d) — SX1262 not detected (SPI order/pins?)\n", st);
        setState(LORA_RADIO_FAIL);
        return false;
    }
    radio.setDio2AsRfSwitch(true);      // proven WisDuo config; ANT_SW GPIO4 is not driven
    node.setDwellTime(true, 400);       // AS923 mandatory 400 ms uplink dwell

    devEui  = (LORA_DEV_EUI != 0ULL) ? (uint64_t)LORA_DEV_EUI : devEuiFromMac();
    joinEui = (uint64_t)LORA_JOIN_EUI;

    portENTER_CRITICAL(&mux);
    status.devEui       = devEui;
    status.joinEui      = joinEui;
    status.credsPresent = credsPresent();
    status.devEuiFromMac = (LORA_DEV_EUI == 0ULL);
    portEXIT_CRITICAL(&mux);

    LOG_I("[LoRa] SX1262 up (AS923, TCXO %.1fV, DIO2 RF switch)\n", (double)tcxo);

    cmdQueue = xQueueCreate(LORA_TX_QUEUE_LEN, sizeof(Cmd));
    setState(credsPresent() ? LORA_JOINING : LORA_NO_CREDS);
    xTaskCreatePinnedToCore(loraTask, "lora", LORA_TASK_STACK, nullptr, LORA_TASK_PRIO, &taskHandle, LORA_TASK_CORE);
    return true;
}

bool loraEnqueueScan(const String& uidHex, const String& eventType, uint32_t epoch, uint16_t seq,
                     bool qcStation, bool replay, const String& operatorUidHex) {
    if (!loraIsJoined()) return false;

    lora_scan_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.epoch = epoch;
    ev.seq = seq;
    ev.event_type = eventTypeCode(eventType);
    ev.flags = (qcStation ? LORA_SCAN_FLAG_QC_STATION : 0u) | (replay ? LORA_SCAN_FLAG_REPLAY : 0u);
    if (!lora_hex_to_bytes(uidHex.c_str(), ev.uid, LORA_UID_MAX, &ev.uid_len)) {
        LOG_W("[LoRa] scan not queued: UID '%s' is not <=10 hex bytes\n", uidHex.c_str());
        return false;
    }
    if (!operatorUidHex.isEmpty() && !lora_hex_to_bytes(operatorUidHex.c_str(), ev.op, LORA_UID_MAX, &ev.op_len)) {
        ev.op_len = 0u;   // operator trailer is optional
    }

    Cmd c;
    c.type = CMD_SCAN;
    const size_t n = lora_encode_scan(&ev, c.payload, sizeof(c.payload));
    if (n == 0u) {
        LOG_W("[LoRa] scan not queued: encoder rejected event (type=%s uid_len=%u)\n", eventType.c_str(), ev.uid_len);
        return false;
    }
    c.len = (uint8_t)n;
    enqueue(c);
    LOG_I("[LoRa] queued scan seq=%u (%u bytes)\n", seq, (unsigned)n);
    return true;
}

void loraUpdateAppInfo(const LoraAppInfo& info) {
    portENTER_CRITICAL(&mux);
    appInfo = info;
    portEXIT_CRITICAL(&mux);
}

void loraSetLinkDown(bool down, uint8_t reason) {
    bool changed;
    portENTER_CRITICAL(&mux);
    changed = (status.linkDown != down);
    status.linkDown = down;
    status.offlineReason = down ? reason : (uint8_t)LORA_OFFLINE_NONE;
    portEXIT_CRITICAL(&mux);
    if (changed) {
        LOG_I("[LoRa] link %s (reason %u)\n", down ? "DOWN — offline heartbeat on" : "UP — offline heartbeat off", reason);
        if (down) {
            Cmd c = {};
            c.type = CMD_HEARTBEAT;
            enqueue(c);
        }
    }
}

void loraRequestJoin() {
    Cmd c = {};
    c.type = CMD_JOIN;
    enqueue(c);
}

void loraRequestHeartbeat() {
    Cmd c = {};
    c.type = CMD_HEARTBEAT;
    enqueue(c);
}

void loraClearSession() {
    Cmd c = {};
    c.type = CMD_CLEAR_SESSION;
    enqueue(c);
}

LoraStatus loraGetStatus() {
    portENTER_CRITICAL(&mux);
    LoraStatus s = status;
    portEXIT_CRITICAL(&mux);
    return s;
}

bool loraIsJoined() {
    return getState() == LORA_JOINED;
}

LoraUiState loraUiState() {
    switch (getState()) {
        case LORA_JOINED:     return LORA_UI_JOINED;
        case LORA_JOINING:
        case LORA_BACKOFF:    return LORA_UI_JOINING;
        case LORA_RADIO_FAIL:
        case LORA_NO_CREDS:   return LORA_UI_FAIL;
        default:              return LORA_UI_NONE;
    }
}

const char* loraStateName(LoraState s) {
    switch (s) {
        case LORA_DISABLED:   return "disabled";
        case LORA_RADIO_FAIL: return "radio-fail";
        case LORA_NO_CREDS:   return "no-creds";
        case LORA_JOINING:    return "joining";
        case LORA_BACKOFF:    return "backoff";
        case LORA_JOINED:     return "joined";
        default:              return "?";
    }
}

#endif // BOARD_HAS_LORA
