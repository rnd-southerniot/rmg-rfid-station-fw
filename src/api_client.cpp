#include "api_client.h"
#include "config.h"
#include "log.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// credentials.h provides SERVER_URL and FACTORY_CODE
#include "credentials.h"

ClaimResult apiClaim(const String& mac, const String& factoryCode) {
    ClaimResult result = { false, "", "", "", "", "" };

    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/stations/claim";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["factory_code"] = factoryCode;
    doc["mac"] = mac;
    doc["fw"] = FW_VERSION;

    JsonDocument caps;
    caps["lcd"] = true;
    caps["touch"] = true;
    caps["buzzer"] = true;
    doc["capabilities"] = caps;

    String body;
    serializeJson(doc, body);

    LOG_D("[API] POST %s\n", url.c_str());
    int code = http.POST(body);

    if (code == 200) {
        JsonDocument resp;
        deserializeJson(resp, http.getString());
        result.ok = true;
        result.token = resp["token"].as<String>();
        result.stationPk = resp["station"]["id"].as<String>();
        result.stationId = resp["station"]["station_id"].is<const char*>()
            ? resp["station"]["station_id"].as<String>() : "";
        result.lineId = resp["station"]["line_id"].is<const char*>()
            ? resp["station"]["line_id"].as<String>() : "";
        result.type = resp["station"]["type"].is<const char*>()
            ? resp["station"]["type"].as<String>() : "";
        LOG_I("[API] Claimed OK, token=%s...\n", result.token.substring(0, 12).c_str());
    } else {
        LOG_E("[API] Claim failed: HTTP %d\n", code);
        if (code > 0) {
            LOG_E("[API] %s\n", http.getString().c_str());
        }
    }

    http.end();
    return result;
}

StationInfo apiGetMe(const String& token) {
    StationInfo info = { false, false, "", "", "", "" };

    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/station/me";
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + token);

    LOG_D("[API] GET %s\n", url.c_str());
    int code = http.GET();

    if (code == 200) {
        JsonDocument resp;
        deserializeJson(resp, http.getString());
        info.ok = true;
        info.factoryId = resp["station"]["factory_id"].as<String>();

        bool hasStationId = resp["station"]["station_id"].is<const char*>();
        bool hasLineId = resp["station"]["line_id"].is<const char*>();
        bool hasType = resp["station"]["type"].is<const char*>();

        if (hasStationId) info.stationId = resp["station"]["station_id"].as<String>();
        if (hasLineId)    info.lineId = resp["station"]["line_id"].as<String>();
        if (hasType)      info.type = resp["station"]["type"].as<String>();

        info.mapped = hasStationId && hasLineId && hasType;
        LOG_I("[API] /me: mapped=%d station_id=%s type=%s\n",
            info.mapped, info.stationId.c_str(), info.type.c_str());
    } else {
        LOG_E("[API] /me failed: HTTP %d\n", code);
    }

    http.end();
    return info;
}

bool apiHeartbeat(const String& token, const String& ts) {
    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/station/heartbeat";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    JsonDocument doc;
    doc["ts"] = ts;

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    bool ok = (code == 200);
    if (!ok) {
        LOG_W("[API] Heartbeat failed: HTTP %d\n", code);
    }

    http.end();
    return ok;
}

LoginResult apiLogin(const String& rfidUid) {
    LoginResult result = { LOGIN_NETWORK_ERROR, "" };

    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/auth/login";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["rfid_uid"] = rfidUid;

    String body;
    serializeJson(doc, body);

    LOG_I("[API] POST /auth/login uid=%s\n", rfidUid.c_str());
    int code = http.POST(body);

    if (code == 200) {
        JsonDocument resp;
        deserializeJson(resp, http.getString());
        result.status = LOGIN_OK;
        result.token  = resp["token"].as<String>();
        LOG_I("[API] Login OK, token=%s...\n", result.token.substring(0, 12).c_str());
    } else if (code == 401) {
        result.status = LOGIN_INVALID_CARD;
        LOG_W("[API] Login rejected (401)\n");
    } else if (code == 400) {
        result.status = LOGIN_BAD_REQUEST;
        LOG_E("[API] Login bad request: %s\n", http.getString().c_str());
    } else {
        result.status = LOGIN_NETWORK_ERROR;
        LOG_E("[API] Login network error: HTTP %d\n", code);
    }

    http.end();
    return result;
}

EventResult apiPostEvent(const String& stationToken, const String& userJwt,
                         const String& eventId, const String& ts,
                         const String& rfidUid, const String& eventType) {
    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/events";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + stationToken);
    http.addHeader("X-User-Token", userJwt);

    JsonDocument doc;
    doc["event_id"] = eventId;
    doc["ts"] = ts;
    doc["event_type"] = eventType;

    JsonDocument bundle;
    bundle["rfid_uid"] = rfidUid;
    doc["bundle"] = bundle;

    String body;
    serializeJson(doc, body);

    LOG_I("[API] POST event: %s %s %s\n", rfidUid.c_str(), eventType.c_str(), eventId.c_str());
    int code = http.POST(body);
    String respBody = http.getString();

    EventResult result;
    if (code == 200) {
        JsonDocument resp;
        deserializeJson(resp, respBody);
        const char* action = resp["action"] | "recorded";
        if (strcmp(action, "logout") == 0) {
            result = EVENT_LOGOUT;
        } else if (strcmp(action, "registered") == 0) {
            result = EVENT_REGISTERED;
        } else {
            result = EVENT_RECORDED;
        }
    } else if (code == 401) {
        // Distinguish station-token failure from user-token failure by error
        // payload. The station path triggers re-claim; the user path triggers
        // logout-and-relogin.
        if (respBody.indexOf("missing_user_token") >= 0
            || respBody.indexOf("invalid_user_token") >= 0) {
            result = EVENT_USER_TOKEN_INVALID;
        } else {
            result = EVENT_STATION_UNAUTHORIZED;
        }
    } else if (code == 409) {
        if (respBody.indexOf("station_unmapped") >= 0) {
            result = EVENT_UNMAPPED;
        } else {
            result = EVENT_TYPE_MISMATCH;
        }
    } else if (code == 400) {
        result = EVENT_INVALID;
        LOG_E("[API] Event invalid: %s\n", respBody.c_str());
    } else {
        result = EVENT_NETWORK_ERROR;
        LOG_E("[API] Event network error: HTTP %d\n", code);
    }

    http.end();
    return result;
}
