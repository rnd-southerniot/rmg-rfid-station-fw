#include <Arduino.h>
#include "config.h"
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

// credentials.h provides FACTORY_CODE
#include "credentials.h"

// ── State machine ─────────────────────────────────────────
enum State {
    STATE_BOOT,
    STATE_CLAIMING,
    STATE_CHECK_MAPPING,
    STATE_UNMAPPED,
    STATE_READY,
    STATE_SCANNING,
    STATE_QC_WAIT,
    STATE_RECONNECTING
};

static State state = STATE_BOOT;
static String token;
static String stationId;
static String lineId;
static String stationType;
static String mac;

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

// Event counter for unique IDs
static uint32_t eventCounter = 0;

// ── Forward declarations ──────────────────────────────────
static void handleEventResult(const String& uid, const String& eventType,
                               EventResult res, const String& eventId, const String& ts);

// ── Helpers ───────────────────────────────────────────────

static String generateEventId() {
    eventCounter++;
    char buf[40];
    snprintf(buf, sizeof(buf), "E_%lu_%u", millis(), eventCounter);
    return String(buf);
}

static void flushEventQueue() {
    int count = eventQueueSize();
    if (count == 0) return;

    Serial.printf("[Main] Flushing %d queued events\n", count);
    int flushed = 0;

    while (flushed < count) {
        QueuedEvent evt;
        if (!eventQueuePop(evt)) break;

        EventResult res = apiPostEvent(token, evt.eventId, evt.ts, evt.rfidUid, evt.eventType);
        if (res == EVENT_OK) {
            flushed++;
            Serial.printf("[Main] Flushed queued event: %s\n", evt.eventId.c_str());
        } else if (res == EVENT_UNAUTHORIZED) {
            // Need to re-claim, stop flushing
            Serial.println("[Main] Queue flush: unauthorized, need re-claim");
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

    Serial.printf("[Main] Queue flush done, %d remaining\n", eventQueueSize());
}

// ── State handlers ────────────────────────────────────────

static void handleBoot() {
    Serial.println("\n[Main] === RMG RFID Station Firmware v" FW_VERSION " ===");

    // Initialize peripherals
    displayInit();
    ledBuzzerInit();
    touchInit();
    storageInit();
    eventQueueInit();

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

    // Show POST results
    displayPostScreen();
    displayPostResult(0, "LCD", true);
    displayPostResult(1, "LED (R/G/B)", true);
    displayPostResult(2, "Buzzer", true);
    displayPostResult(3, "RFID (MFRC522)", rfidOk);
    displayPostResult(4, "Touch (FT6336)", touchOk);

    Serial.printf("[POST] LCD=OK LED=OK Buzzer=OK RFID=%s Touch=%s\n",
        rfidOk ? "OK" : "FAIL", touchOk ? "OK" : "FAIL");

    delay(1500);

    displayBootScreen("Connecting to WiFi...");
    ledBlue();

    wifiInit();

    if (!wifiIsConnected()) {
        displayBootScreen("WiFi failed, retrying...");
        ledRed();
        state = STATE_RECONNECTING;
        return;
    }

    mac = wifiGetMac();
    Serial.printf("[Main] MAC: %s\n", mac.c_str());

    displayBootScreen("Syncing time...");
    ntpInit();

    // Start OTA
    String otaHostname = "rfid-" + mac;
    otaHostname.replace(":", "");
    otaInit(otaHostname);

    // Load saved token
    token = storageLoadToken();
    if (token.isEmpty()) {
        state = STATE_CLAIMING;
    } else {
        Serial.println("[Main] Loaded saved token");
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
        Serial.printf("[Main] Claimed, station PK: %s\n", result.stationPk.c_str());
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
        Serial.println("[Main] /me failed, might need re-claim");
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
        displayReadyScreen(stationId, lineId, stationType);
        displayStatusBar(wifiIsConnected(), stationId, ntpGetTimeStr());
        ledGreen();
        delay(500);
        ledOff();
        state = STATE_READY;
        lastHeartbeat = millis();
        lastMappingPoll = millis();
        lastStatusBar = millis();
        Serial.printf("[Main] READY: %s / %s / %s\n",
            stationId.c_str(), lineId.c_str(), stationType.c_str());
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

static void handleReady() {
    unsigned long now = millis();

    // Check WiFi
    if (!wifiIsConnected()) {
        state = STATE_RECONNECTING;
        return;
    }

    // Heartbeat
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        String ts = ntpGetIsoTimestamp();
        if (!apiHeartbeat(token, ts)) {
            Serial.println("[Main] Heartbeat failed");
        }
    }

    // Mapping poll
    if (now - lastMappingPoll >= MAPPING_POLL_INTERVAL_MS) {
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
        displayStatusBar(wifiIsConnected(), stationId, ntpGetTimeStr());
    }

    // Flush offline queue periodically
    if (now - lastQueueFlush >= 60000 && eventQueueSize() > 0) {
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

        Serial.printf("[Main] Scanned UID: %s\n", uid.c_str());

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

            EventResult res = apiPostEvent(token, eventId, ts, uid, "COMPLETE");
            handleEventResult(uid, "COMPLETE", res, eventId, ts);
        }
    }
}

static void handleEventResult(const String& uid, const String& eventType,
                               EventResult res, const String& eventId, const String& ts) {
    switch (res) {
        case EVENT_OK:
            displayScanResult(uid, eventType, true, "OK");
            ledGreen();
            beepSuccess();
            break;
        case EVENT_UNKNOWN_BUNDLE:
            displayScanResult(uid, eventType, false, "Unknown Tag");
            ledYellow();
            beepWarning();
            break;
        case EVENT_UNMAPPED:
            displayScanResult(uid, eventType, false, "Not Mapped");
            ledRed();
            beepError();
            // Re-check mapping
            lastMappingPoll = 0;
            break;
        case EVENT_UNAUTHORIZED:
            displayScanResult(uid, eventType, false, "Auth Error");
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
        case EVENT_NETWORK_ERROR:
            displayScanResult(uid, eventType, false, "Queued (offline)");
            ledYellow();
            beepWarning();
            eventQueuePush(eventId, ts, uid, eventType);
            break;
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
    // Show result for SCAN_RESULT_DISPLAY_MS, then return to ready
    if (millis() - scanResultShownAt >= SCAN_RESULT_DISPLAY_MS) {
        ledOff();
        displayReadyScreen(stationId, lineId, stationType);
        state = STATE_READY;
    }
}

static void handleQcWait() {
    unsigned long now = millis();

    // Timeout
    if (now - qcWaitStartedAt >= QC_TOUCH_TIMEOUT_MS) {
        Serial.println("[Main] QC timeout");
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

            EventResult res = apiPostEvent(token, eventId, ts, pendingRfidUid, eventType);
            handleEventResult(pendingRfidUid, eventType, res, eventId, ts);
        }
    }
}

static void handleReconnecting() {
    unsigned long now = millis();

    if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetry = now;
        displayBootScreen("Reconnecting WiFi...");
        ledRed();
        wifiReconnect();

        if (wifiIsConnected()) {
            Serial.println("[Main] WiFi reconnected");
            ledOff();

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
    delay(100);
    state = STATE_BOOT;
}

void loop() {
    otaHandle();

    switch (state) {
        case STATE_BOOT:          handleBoot(); break;
        case STATE_CLAIMING:      handleClaiming(); break;
        case STATE_CHECK_MAPPING: handleCheckMapping(); break;
        case STATE_UNMAPPED:      handleUnmapped(); break;
        case STATE_READY:         handleReady(); break;
        case STATE_SCANNING:      handleScanning(); break;
        case STATE_QC_WAIT:       handleQcWait(); break;
        case STATE_RECONNECTING:  handleReconnecting(); break;
    }

    delay(10); // Small yield
}
