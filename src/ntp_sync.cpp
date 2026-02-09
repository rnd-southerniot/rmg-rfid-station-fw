#include "ntp_sync.h"
#include "config.h"
#include <time.h>

void ntpInit() {
    configTime(0, 0, NTP_SERVER);
    Serial.print("[NTP] Syncing");
    struct tm t;
    int attempts = 0;
    while (!getLocalTime(&t) && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (getLocalTime(&t)) {
        Serial.printf("\n[NTP] Synced: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        Serial.println("\n[NTP] Sync failed, using millis fallback");
    }
}

bool ntpIsSynced() {
    struct tm t;
    return getLocalTime(&t);
}

String ntpGetIsoTimestamp() {
    struct tm t;
    if (getLocalTime(&t)) {
        char buf[30];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec);
        return String(buf);
    }
    // Fallback: epoch + millis (not ideal but avoids empty string)
    return String("1970-01-01T00:00:00Z");
}
