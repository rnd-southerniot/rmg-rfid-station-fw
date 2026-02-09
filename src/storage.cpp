#include "storage.h"
#include <Preferences.h>

static Preferences prefs;

void storageInit() {
    prefs.begin("rfid-station", false);
    Serial.println("[Storage] NVS initialized");
}

void storageSaveToken(const String& token) {
    prefs.putString("token", token);
    Serial.println("[Storage] Token saved");
}

String storageLoadToken() {
    return prefs.getString("token", "");
}

void storageClearToken() {
    prefs.remove("token");
    Serial.println("[Storage] Token cleared");
}

void storageSaveStationInfo(const String& stationId, const String& lineId, const String& type) {
    prefs.putString("station_id", stationId);
    prefs.putString("line_id", lineId);
    prefs.putString("type", type);
}

String storageLoadStationId() { return prefs.getString("station_id", ""); }
String storageLoadLineId()    { return prefs.getString("line_id", ""); }
String storageLoadType()      { return prefs.getString("type", ""); }
