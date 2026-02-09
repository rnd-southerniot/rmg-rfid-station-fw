#pragma once
#include <Arduino.h>

struct ClaimResult {
    bool ok;
    String token;
    String stationPk;     // internal "st_..." ID
    String stationId;     // logical ID (null if unmapped)
    String lineId;
    String type;
};

struct StationInfo {
    bool ok;
    bool mapped;          // true if station_id + line_id + type all present
    String stationId;
    String lineId;
    String type;
    String factoryId;
};

enum EventResult {
    EVENT_OK,
    EVENT_UNAUTHORIZED,
    EVENT_UNKNOWN_BUNDLE,
    EVENT_UNMAPPED,
    EVENT_TYPE_MISMATCH,
    EVENT_INVALID,
    EVENT_NETWORK_ERROR
};

ClaimResult apiClaim(const String& mac, const String& factoryCode);
StationInfo apiGetMe(const String& token);
bool apiHeartbeat(const String& token, const String& ts);
EventResult apiPostEvent(const String& token, const String& eventId, const String& ts,
                         const String& rfidUid, const String& eventType);
