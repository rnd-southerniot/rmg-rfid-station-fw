#include "storage.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>

static Preferences prefs;
static uint16_t seq = 0;

#define SEQ_CHECKPOINT 64

void storageInit() {
    prefs.begin("rfid-station", false);
    // Jump past every value that could have been handed out before the last checkpoint.
    seq = (uint16_t)(prefs.getUShort("seq", 0) + SEQ_CHECKPOINT);
    prefs.putUShort("seq", seq);
    LOG_I("[Storage] NVS initialized, event seq starts at %u\n", seq);
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

void storageSaveJwt(const String& jwt) {
    prefs.putString("user_jwt", jwt);
    LOG_D("[Storage] User JWT saved (%u chars)\n", (unsigned)jwt.length());
}

String storageLoadJwt() {
    return prefs.getString("user_jwt", "");
}

void storageClearJwt() {
    prefs.remove("user_jwt");
    LOG_W("[Storage] User JWT cleared\n");
}

uint16_t storageNextSeq() {
    seq++;
    if ((seq % SEQ_CHECKPOINT) == 0) {
        prefs.putUShort("seq", seq);
    }
    return seq;
}

uint16_t storageCurrentSeq() {
    return seq;
}
