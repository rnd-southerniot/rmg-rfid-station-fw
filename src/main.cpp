#include <Arduino.h>
#include "config.h"
#include "log.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "storage.h"
#include "api_client.h"
#include "rfid_reader.h"
#include "display.h"
#include "touch.h"
#include "led_buzzer.h"
#include "event_queue.h"
#include "ota.h"
#include "lora_link.h"
#include "serial_console.h"
#include "post_results.h"

// credentials.h provides FACTORY_CODE
#include "credentials.h"

// ── State machine ─────────────────────────────────────────
enum State {
    STATE_BOOT,
    STATE_CLAIMING,
    STATE_CHECK_MAPPING,
    STATE_UNMAPPED,
    STATE_LOGIN,
    STATE_READY,
    STATE_SCANNING,
    STATE_QC_WAIT,
    STATE_RECONNECTING
};

static State state = STATE_BOOT;
static String token;          // station bearer
static String userJwt;        // operator JWT from /auth/login
static String stationId;
static String lineId;
static String stationType;
static String mac;
static unsigned long lastUserActivityAt = 0; // for inactivity timeout
static String operatorUid;          // badge UID of the logged-in operator (LoRa payload only)
static bool   readyOffline = false; // READY running without WiFi (LoRa fallback mode)
static int    httpFailStreak = 0;   // consecutive HTTP failures while WiFi is up

// Timers
static unsigned long lastHeartbeat = 0;
static unsigned long lastMappingPoll = 0;
static unsigned long lastWifiRetry = 0;
static unsigned long scanResultShownAt = 0;
static unsigned long qcWaitStartedAt = 0;
static unsigned long lastQueueFlush = 0;
static unsigned long lastStatusBar = 0;

// Scan debounce
static String lastScannedUid;
static unsigned long lastScanTime = 0;

PostResults g_post = {};

// Identity of the last generated event, shared by the HTTP event_id and the LoRa payload
static uint32_t lastEpoch = 0;
static uint16_t lastSeq = 0;

// ── Forward declarations ──────────────────────────────────
static void handleEventResult(const String& uid, const String& eventType,
                               EventResult res, const String& eventId, const String& ts);

// ── Helpers ───────────────────────────────────────────────

static String generateEventId() {
    // "E_<epoch>_<seq>": the same (epoch, seq) pair travels in the LoRa fPort-10 frame, so the
    // backend can de-duplicate an HTTP replay against a LoRa copy of the same scan.
    lastEpoch = ntpGetEpoch();
    lastSeq = storageNextSeq();
    char buf[40];
    snprintf(buf, sizeof(buf), "E_%lu_%u", (unsigned long)lastEpoch, (unsigned)lastSeq);
    return String(buf);
}

// LoRa fallback: keep scanning without WiFi when the radio is joined and both the station
// mapping and the operator session are cached. Always false on boards without a radio.
static bool offlineModeAllowed() {
    return loraIsJoined() && !stationId.isEmpty() && !userJwt.isEmpty();
}

static void updateLoraAppInfo() {
    LoraAppInfo info;
    info.queuedHttpEvents = (uint16_t)eventQueueSize();
    info.lastSeq = lastSeq;
    info.operatorLoggedIn = !userJwt.isEmpty();
    info.stationMapped = !stationId.isEmpty();
    info.wifiAssociated = wifiIsConnected();
    info.wifiRssi = wifiGetRssi();
    loraUpdateAppInfo(info);
}

static void refreshStatusBar() {
    updateLoraAppInfo();
    displayStatusBar(wifiIsConnected(), stationId, ntpGetTimeStr(), loraUiState());
}

// Track backend reachability while WiFi is up (WiFi-down is reported separately).
static void noteHttpResult(bool ok) {
    if (ok) {
        httpFailStreak = 0;
        if (wifiIsConnected()) loraSetLinkDown(false, 0);
        return;
    }
    if (++httpFailStreak >= 2 && wifiIsConnected()) {
        loraSetLinkDown(true, LORA_OFFLINE_SERVER_UNREACHABLE);
    }
}

static void clearOperatorSession() {
    storageClearJwt();
    userJwt = "";
    operatorUid = "";
}

// Post over HTTP when WiFi is up; otherwise report a network error straight away so the
// event is queued for replay (and mirrored over LoRa) without a blocking connect attempt.
static EventResult postEventOrQueue(const String& eventId, const String& ts,
                                    const String& uid, const String& eventType) {
    if (!wifiIsConnected()) return EVENT_NETWORK_ERROR;
    EventResult res = apiPostEvent(token, userJwt, eventId, ts, uid, eventType);
    noteHttpResult(res != EVENT_NETWORK_ERROR);
    return res;
}

static void flushEventQueue() {
    int count = eventQueueSize();
    if (count == 0) return;

    LOG_I("[Main] Flushing %d queued events\n", count);
    int flushed = 0;

    while (flushed < count) {
        QueuedEvent evt;
        if (!eventQueuePop(evt)) break;

        EventResult res = apiPostEvent(token, userJwt, evt.eventId, evt.ts, evt.rfidUid, evt.eventType);
        if (res == EVENT_RECORDED || res == EVENT_REGISTERED) {
            flushed++;
            LOG_D("[Main] Flushed queued event: %s\n", evt.eventId.c_str());
        } else if (res == EVENT_LOGOUT || res == EVENT_USER_TOKEN_INVALID) {
            // Operator session no longer valid — re-queue and force re-login
            eventQueuePush(evt.eventId, evt.ts, evt.rfidUid, evt.eventType);
            LOG_W("[Main] Queue flush: user session invalid, stop flushing\n");
            clearOperatorSession();
            state = STATE_LOGIN;
            break;
        } else if (res == EVENT_STATION_UNAUTHORIZED) {
            // Need to re-claim, stop flushing
            LOG_W("[Main] Queue flush: station unauthorized, need re-claim\n");
            state = STATE_CLAIMING;
            break;
        } else if (res == EVENT_NETWORK_ERROR) {
            // Re-queue and stop
            eventQueuePush(evt.eventId, evt.ts, evt.rfidUid, evt.eventType);
            break;
        } else {
            // Other errors (unknown_bundle, etc) — discard after too many retries
            if (evt.retries < 5) {
                evt.retries++;
                eventQueuePush(evt.eventId, evt.ts, evt.rfidUid, evt.eventType);
            }
            flushed++;
        }
    }

    LOG_I("[Main] Queue flush done, %d remaining\n", eventQueueSize());
}

// ── State handlers ────────────────────────────────────────

static void handleBoot() {
    LOG_I("\n[Main] === RMG RFID Station Firmware v" FW_VERSION " ===\n");

    // Initialize peripherals
    displayInit();
    ledBuzzerInit();
    touchInit();
    storageInit();
    eventQueueInit();
    consoleInit();

    // ── Power-on self-test ──
    displayBootScreen("Self-test...");

    // LED test: flash R, G, B
    ledRed(); delay(200); ledGreen(); delay(200); ledBlue(); delay(200); ledOff();

    // Buzzer test
    beepSuccess();

    // Init RFID for POST check
    rfidInit();
    uint8_t rfidVer = rfidGetVersion();
    bool rfidOk = (rfidVer != 0x00 && rfidVer != 0xFF);
    bool touchOk = touchIsConnected();
    bool loraOk = loraInit();   // no-op (false) on boards without a radio
    (void)loraOk;

    bool lcdOk = true;
    uint16_t lcdId = 0;
#if LCD_POST_READ_ID
    lcdId = displayReadId();
    lcdOk = (lcdId == 0x9341);
    LOG_I("[Display] ILI9341 RDID4 = 0x%04X (%s)\n", lcdId, lcdOk ? "OK" : "unexpected");
#endif

    g_post.valid = true;
    g_post.lcd = lcdOk;
    g_post.lcdId = lcdId;
    g_post.led = true;
    g_post.buzzer = true;
    g_post.rfid = rfidOk;
    g_post.rfidVer = rfidVer;
    g_post.touch = touchOk;
    g_post.lora = loraOk;

    // Show POST results
    displayPostScreen();
    displayPostResult(0, "LCD", lcdOk);
    displayPostResult(1, "LED (" LED_NAME ")", true);
    displayPostResult(2, "Buzzer", true);
    displayPostResult(3, "RFID (" RFID_READER_NAME ")", rfidOk);
    displayPostResult(4, "Touch (FT6336)", touchOk);
#if BOARD_HAS_LORA
    displayPostResult(5, "LoRa (SX1262)", loraOk);
    LOG_I("[POST] LoRa=%s\n", loraOk ? "OK" : "FAIL");
#endif
#ifdef BOARD_HAS_PSRAM
    LOG_I("[Main] PSRAM: %u KB\n", (unsigned)(ESP.getPsramSize() / 1024));
#endif

    LOG_I("[POST] LCD=OK LED=OK Buzzer=OK RFID=%s Touch=%s\n",
        rfidOk ? "OK" : "FAIL", touchOk ? "OK" : "FAIL");

    delay(1500);

    // Restore the persisted identity before touching the network, so a boot without WiFi can
    // still run the LoRa fallback and a later reconnect does not re-claim needlessly. The JWT
    // is loaded eagerly but only verified lazily on the next /events POST.
    token = storageLoadToken();
    userJwt = storageLoadJwt();
    stationId = storageLoadStationId();
    lineId = storageLoadLineId();
    stationType = storageLoadType();

    displayBootScreen("Connecting to WiFi...");
    ledBlue();

    wifiInit();

    if (!wifiIsConnected()) {
        displayBootScreen("WiFi failed, retrying...");
        ledRed();
        loraSetLinkDown(true, LORA_OFFLINE_BOOT_NO_WIFI);
        state = STATE_RECONNECTING;
        return;
    }

    mac = wifiGetMac();
    LOG_I("[Main] MAC: %s\n", mac.c_str());

    displayBootScreen("Syncing time...");
    ntpInit();

    // Start OTA
    String otaHostname = "rfid-" + mac;
    otaHostname.replace(":", "");
    otaInit(otaHostname);

    if (!userJwt.isEmpty()) {
        LOG_D("[Main] Loaded saved user JWT (%u chars)\n", (unsigned)userJwt.length());
    }
    if (token.isEmpty()) {
        state = STATE_CLAIMING;
    } else {
        LOG_D("[Main] Loaded saved station token\n");
        state = STATE_CHECK_MAPPING;
    }

    ledOff();
    beepSuccess();
}

static void handleClaiming() {
    displayClaimingScreen();
    ledBlue();

    ClaimResult result = apiClaim(mac, FACTORY_CODE);
    if (result.ok) {
        token = result.token;
        storageSaveToken(token);
        LOG_I("[Main] Claimed, station PK: %s\n", result.stationPk.c_str());
        ledOff();
        state = STATE_CHECK_MAPPING;
    } else {
        displayError("Claim failed — retrying in 5s");
        ledRed();
        beepError();
        delay(5000);
        // Stay in claiming state to retry
    }
}

static void handleCheckMapping() {
    StationInfo info = apiGetMe(token);

    if (!info.ok) {
        LOG_W("[Main] /me failed, might need re-claim\n");
        storageClearToken();
        token = "";
        state = STATE_CLAIMING;
        return;
    }

    if (info.mapped) {
        stationId = info.stationId;
        lineId = info.lineId;
        stationType = info.type;
        storageSaveStationInfo(stationId, lineId, stationType);

        if (userJwt.isEmpty()) {
            // No operator session — go to login screen
            displayLoginScreen(stationId);
            ledBlue();
            state = STATE_LOGIN;
            lastHeartbeat = millis();
            lastMappingPoll = millis();
            lastStatusBar = millis();
            LOG_I("[Main] LOGIN: %s / %s / %s (no JWT)\n",
                stationId.c_str(), lineId.c_str(), stationType.c_str());
        } else {
            displayReadyScreen(stationId, lineId, stationType);
            refreshStatusBar();
            ledGreen();
            delay(500);
            ledOff();
            state = STATE_READY;
            lastHeartbeat = millis();
            lastMappingPoll = millis();
            lastStatusBar = millis();
            lastUserActivityAt = millis();
            LOG_I("[Main] READY: %s / %s / %s (logged in)\n",
                stationId.c_str(), lineId.c_str(), stationType.c_str());
        }
    } else {
        displayUnmappedScreen(mac);
        ledYellow();
        state = STATE_UNMAPPED;
        lastMappingPoll = millis();
    }
}

static void handleUnmapped() {
    // Poll for mapping every MAPPING_POLL_INTERVAL_MS
    if (millis() - lastMappingPoll >= MAPPING_POLL_INTERVAL_MS) {
        lastMappingPoll = millis();
        state = STATE_CHECK_MAPPING;
    }
}

static String pendingRfidUid;

static void handleLogin() {
    unsigned long now = millis();

    if (!wifiIsConnected()) {
        if (!(loraIsJoined() && !stationId.isEmpty())) {
            state = STATE_RECONNECTING;
            return;
        }
        // LoRa fallback: login needs the backend, so keep the login screen with an offline
        // note, retry WiFi in the background, and refuse taps politely.
        loraSetLinkDown(true, LORA_OFFLINE_WIFI_DOWN);
        displayLoginScreen(stationId, true);
        if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
            lastWifiRetry = now;
            wifiReconnect();
        }
        if (rfidCardPresent()) {
            String uid = rfidReadUid();
            if (!uid.isEmpty() && !(uid == lastScannedUid && (now - lastScanTime) < SCAN_DEBOUNCE_MS)) {
                lastScannedUid = uid;
                lastScanTime = now;
                beepWarning();
                ledYellow();
                displayScanResult(uid, "LOGIN", false, "Offline - no login");
                scanResultShownAt = now;
                state = STATE_SCANNING;
            }
        }
        return;
    }
    displayLoginScreen(stationId, false);   // drops the offline note once WiFi is back

    // Keep the login screen up. No heartbeat skipping — station heartbeat
    // does not require user auth, so keep it running so the device stays
    // visible to the admin UI even when nobody is logged in.
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        String ts = ntpGetIsoTimestamp();
        bool hbOk = apiHeartbeat(token, ts);
        noteHttpResult(hbOk);
        if (!hbOk) {
            LOG_W("[Main] Heartbeat failed (login screen)\n");
        }
    }

    // Re-paint login screen periodically so status bar stays fresh.
    if (now - lastStatusBar >= 2000) {
        lastStatusBar = now;
        refreshStatusBar();
    }

    // Wait for badge tap
    if (!rfidCardPresent()) return;

    String uid = rfidReadUid();
    if (uid.isEmpty()) return;

    if (uid == lastScannedUid && (now - lastScanTime) < SCAN_DEBOUNCE_MS) return;
    lastScannedUid = uid;
    lastScanTime = now;

    LOG_I("[Main] Login attempt with UID %s\n", uid.c_str());
    LoginResult lr = apiLogin(uid);

    switch (lr.status) {
        case LOGIN_OK:
            userJwt = lr.token;
            storageSaveJwt(userJwt);
            operatorUid = uid;
            beepSuccess();
            ledGreen();
            displayScanResult(uid, "LOGIN", true, "Logged In");
            scanResultShownAt = now;
            // After result shown, transition to ready (handled in handleScanning)
            state = STATE_SCANNING;
            // After scanning result display ends, handleScanning falls back to
            // READY only if we are logged in; mark JWT before that so the
            // transition picks the right state.
            lastUserActivityAt = now;
            break;

        case LOGIN_INVALID_CARD:
            beepError();
            ledRed();
            displayScanResult(uid, "LOGIN", false, "Invalid Card");
            scanResultShownAt = now;
            state = STATE_SCANNING;
            break;

        case LOGIN_BAD_REQUEST:
            beepError();
            ledRed();
            displayScanResult(uid, "LOGIN", false, "Bad Request");
            scanResultShownAt = now;
            state = STATE_SCANNING;
            break;

        case LOGIN_NETWORK_ERROR:
        default:
            beepWarning();
            ledYellow();
            displayScanResult(uid, "LOGIN", false, "Network Error");
            scanResultShownAt = now;
            state = STATE_SCANNING;
            break;
    }
}

static void handleReady() {
    unsigned long now = millis();

    // Check WiFi. With a joined LoRa link and a cached mapping + operator session the station
    // keeps scanning offline: events queue for HTTP replay and a copy goes out over LoRa.
    if (!wifiIsConnected()) {
        if (!offlineModeAllowed()) {
            state = STATE_RECONNECTING;
            return;
        }
        if (!readyOffline) {
            readyOffline = true;
            LOG_W("[Main] WiFi down — READY continues in LoRa fallback mode\n");
            loraSetLinkDown(true, LORA_OFFLINE_WIFI_DOWN);
            refreshStatusBar();
        }
        if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
            lastWifiRetry = now;
            wifiReconnect();
        }
    } else if (readyOffline) {
        readyOffline = false;
        LOG_I("[Main] WiFi back — leaving LoRa fallback mode\n");
        loraSetLinkDown(false, 0);
        if (eventQueueSize() > 0) {
            flushEventQueue();
        }
    }
    const bool online = wifiIsConnected();

    // Inactivity logout: if no scan activity for INACTIVITY_TIMEOUT_MS,
    // clear the JWT and return to login screen. 0 disables.
    if (INACTIVITY_TIMEOUT_MS > 0 && !userJwt.isEmpty()
        && (now - lastUserActivityAt) >= INACTIVITY_TIMEOUT_MS) {
        LOG_W("[Main] Inactivity timeout — logging out\n");
        clearOperatorSession();
        beepWarning();
        ledYellow();
        delay(200);
        ledOff();
        displayLoginScreen(stationId);
        state = STATE_LOGIN;
        return;
    }

    // Heartbeat
    if (online && now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        String ts = ntpGetIsoTimestamp();
        bool hbOk = apiHeartbeat(token, ts);
        noteHttpResult(hbOk);
        if (!hbOk) {
            LOG_W("[Main] Heartbeat failed\n");
        }
    }

    // Mapping poll
    if (online && now - lastMappingPoll >= MAPPING_POLL_INTERVAL_MS) {
        lastMappingPoll = now;
        StationInfo info = apiGetMe(token);
        if (info.ok && info.mapped) {
            // Update if mapping changed
            if (info.stationId != stationId || info.type != stationType) {
                stationId = info.stationId;
                lineId = info.lineId;
                stationType = info.type;
                storageSaveStationInfo(stationId, lineId, stationType);
                displayReadyScreen(stationId, lineId, stationType);
            }
        }
    }

    // Update status bar every 2s
    if (now - lastStatusBar >= 2000) {
        lastStatusBar = now;
        refreshStatusBar();
    }

    // Flush offline queue periodically
    if (online && now - lastQueueFlush >= 60000 && eventQueueSize() > 0) {
        lastQueueFlush = now;
        flushEventQueue();
    }

    // Check for RFID scan
    if (rfidCardPresent()) {
        String uid = rfidReadUid();
        if (uid.isEmpty()) return;

        // Debounce: ignore same UID within SCAN_DEBOUNCE_MS
        if (uid == lastScannedUid && (now - lastScanTime) < SCAN_DEBOUNCE_MS) {
            return;
        }
        lastScannedUid = uid;
        lastScanTime = now;

        LOG_I("[Main] Scanned UID: %s\n", uid.c_str());

        lastUserActivityAt = now;

        if (stationType == "qc") {
            // QC station: show PASS/FAIL buttons
            pendingRfidUid = uid;
            displayQcButtons(uid);
            qcWaitStartedAt = now;
            state = STATE_QC_WAIT;
        } else {
            // Non-QC station: auto-send COMPLETE
            state = STATE_SCANNING;
            String eventId = generateEventId();
            String ts = ntpGetIsoTimestamp();

            EventResult res = postEventOrQueue(eventId, ts, uid, "COMPLETE");
            handleEventResult(uid, "COMPLETE", res, eventId, ts);
        }
    }
}

static void handleEventResult(const String& uid, const String& eventType,
                               EventResult res, const String& eventId, const String& ts) {
    switch (res) {
        case EVENT_RECORDED:
            displayScanResult(uid, eventType, true, "OK");
            ledGreen();
            beepSuccess();
            break;
        case EVENT_REGISTERED:
            displayScanResult(uid, eventType, true, "RFID Registered");
            ledYellow();
            beepWarning();
            break;
        case EVENT_LOGOUT:
            displayScanResult(uid, eventType, true, "Logged Out");
            ledBlue();
            beepSuccess();
            clearOperatorSession();
            // handleScanning will route to STATE_LOGIN once result times out.
            break;
        case EVENT_USER_TOKEN_INVALID:
            displayScanResult(uid, eventType, false, "Session Expired");
            ledYellow();
            beepWarning();
            clearOperatorSession();
            // handleScanning will route to STATE_LOGIN once result times out.
            break;
        case EVENT_UNMAPPED:
            displayScanResult(uid, eventType, false, "Not Mapped");
            ledRed();
            beepError();
            // Re-check mapping
            lastMappingPoll = 0;
            break;
        case EVENT_STATION_UNAUTHORIZED:
            displayScanResult(uid, eventType, false, "Station Auth Error");
            ledRed();
            beepError();
            storageClearToken();
            token = "";
            state = STATE_CLAIMING;
            return;
        case EVENT_TYPE_MISMATCH:
            displayScanResult(uid, eventType, false, "Type Mismatch");
            ledRed();
            beepError();
            break;
        case EVENT_NETWORK_ERROR: {
            eventQueuePush(eventId, ts, uid, eventType);
            // LoRa fallback: a copy goes out now; the HTTP replay later carries the same seq.
            bool viaLora = loraIsJoined() &&
                           loraEnqueueScan(uid, eventType, lastEpoch, lastSeq,
                                           stationType == "qc", false, operatorUid);
            displayScanResult(uid, eventType, false, viaLora ? "Queued + LoRa" : "Queued (offline)");
            ledYellow();
            beepWarning();
            break;
        }
        case EVENT_INVALID:
            displayScanResult(uid, eventType, false, "Invalid Data");
            ledRed();
            beepError();
            break;
    }

    scanResultShownAt = millis();
    state = STATE_SCANNING;
}

static void handleScanning() {
    // Show result for SCAN_RESULT_DISPLAY_MS, then return to whichever screen
    // matches the current auth state.
    if (millis() - scanResultShownAt >= SCAN_RESULT_DISPLAY_MS) {
        ledOff();
        if (userJwt.isEmpty()) {
            displayLoginScreen(stationId);
            state = STATE_LOGIN;
        } else {
            displayReadyScreen(stationId, lineId, stationType);
            state = STATE_READY;
            lastUserActivityAt = millis();
        }
    }
}

static void handleQcWait() {
    unsigned long now = millis();

    // Timeout
    if (now - qcWaitStartedAt >= QC_TOUCH_TIMEOUT_MS) {
        LOG_I("[Main] QC timeout\n");
        displayReadyScreen(stationId, lineId, stationType);
        state = STATE_READY;
        return;
    }

    // Check touch
    int tx, ty;
    if (touchGetPoint(tx, ty)) {
        QcChoice choice = touchCheckQcButton(tx, ty);
        if (choice != QC_NONE) {
            String eventType = (choice == QC_PASS_TOUCH) ? "QC_PASS" : "QC_FAIL";
            String eventId = generateEventId();
            String ts = ntpGetIsoTimestamp();

            EventResult res = postEventOrQueue(eventId, ts, pendingRfidUid, eventType);
            handleEventResult(pendingRfidUid, eventType, res, eventId, ts);
        }
    }
}

static void handleReconnecting() {
    unsigned long now = millis();

    loraSetLinkDown(true, LORA_OFFLINE_WIFI_DOWN);

    // LoRa fallback: with a joined link and a cached mapping, run the station offline instead of
    // parking here (READY if an operator session is cached, else the LOGIN screen with a note).
    // Those handlers keep retrying WiFi.
    if (offlineModeAllowed()) {
        LOG_W("[Main] Entering READY in LoRa fallback mode (WiFi down)\n");
        displayReadyScreen(stationId, lineId, stationType);
        ledOff();
        state = STATE_READY;
        lastUserActivityAt = now;
        lastStatusBar = 0;
        return;
    }
    if (loraIsJoined() && !stationId.isEmpty() && userJwt.isEmpty()) {
        displayLoginScreen(stationId, true);
        ledBlue();
        state = STATE_LOGIN;
        return;
    }

    if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetry = now;
        displayBootScreen(loraIsJoined() ? "Reconnecting WiFi... (LoRa joined)" : "Reconnecting WiFi...");
        ledRed();
        wifiReconnect();

        if (wifiIsConnected()) {
            LOG_I("[Main] WiFi reconnected\n");
            ledOff();
            loraSetLinkDown(false, 0);

            // Flush queued events
            if (eventQueueSize() > 0) {
                flushEventQueue();
            }

            // Return to appropriate state
            if (token.isEmpty()) {
                state = STATE_CLAIMING;
            } else {
                state = STATE_CHECK_MAPPING;
            }
        }
    }
}

// ── Arduino entry points ──────────────────────────────────

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(5);   // USB CDC: never let LOG_* stall the loop when no host is attached
#endif
    delay(100);
    state = STATE_BOOT;
}

void loop() {
    otaHandle();
    consoleService();

    switch (state) {
        case STATE_BOOT:          handleBoot(); break;
        case STATE_CLAIMING:      handleClaiming(); break;
        case STATE_CHECK_MAPPING: handleCheckMapping(); break;
        case STATE_UNMAPPED:      handleUnmapped(); break;
        case STATE_LOGIN:         handleLogin(); break;
        case STATE_READY:         handleReady(); break;
        case STATE_SCANNING:      handleScanning(); break;
        case STATE_QC_WAIT:       handleQcWait(); break;
        case STATE_RECONNECTING:  handleReconnecting(); break;
    }

    delay(10); // Small yield
}
