#include "storage.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>

static Preferences prefs;

void storageInit() {
    prefs.begin("rfid-station", false);
    LOG_I("[Storage] NVS initialized\n");
}

void storageSaveToken(const String& token) {
    prefs.putString("token", token);
    LOG_D("[Storage] Token saved\n");
}

String storageLoadToken() {
    return prefs.getString("token", "");
}

void storageClearToken() {
    prefs.remove("token");
    LOG_W("[Storage] Token cleared\n");
}

void storageSaveStationInfo(const String& stationId, const String& lineId, const String& type) {
    prefs.putString("station_id", stationId);
    prefs.putString("line_id", lineId);
    prefs.putString("type", type);
}

String storageLoadStationId() { return prefs.getString("station_id", ""); }
String storageLoadLineId()    { return prefs.getString("line_id", ""); }
String storageLoadType()      { return prefs.getString("type", ""); }
