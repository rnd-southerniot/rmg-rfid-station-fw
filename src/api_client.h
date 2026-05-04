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

enum LoginStatus {
    LOGIN_OK,
    LOGIN_INVALID_CARD,    // 401 invalid_credentials
    LOGIN_BAD_REQUEST,     // 400 — firmware bug
    LOGIN_NETWORK_ERROR    // -1 / 5xx
};

struct LoginResult {
    LoginStatus status;
    String token;
};

enum EventResult {
    EVENT_RECORDED,            // 200 action=recorded
    EVENT_REGISTERED,          // 200 action=registered (UID was unknown bundle)
    EVENT_LOGOUT,              // 200 action=logout (UID is employee badge)
    EVENT_STATION_UNAUTHORIZED,// 401 unauthorized (station bearer bad → re-claim)
    EVENT_USER_TOKEN_INVALID,  // 401 missing_user_token | invalid_user_token
    EVENT_UNMAPPED,            // 409 station_unmapped
    EVENT_TYPE_MISMATCH,       // 409 station_type_mismatch
    EVENT_INVALID,             // 400 invalid_request
    EVENT_NETWORK_ERROR        // transport / 5xx
};

ClaimResult apiClaim(const String& mac, const String& factoryCode);
StationInfo apiGetMe(const String& token);
bool apiHeartbeat(const String& token, const String& ts);
LoginResult apiLogin(const String& rfidUid);
EventResult apiPostEvent(const String& stationToken, const String& userJwt,
                         const String& eventId, const String& ts,
                         const String& rfidUid, const String& eventType);
