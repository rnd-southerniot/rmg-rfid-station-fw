#include "api_client.h"
#include "config.h"
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

    Serial.printf("[API] POST %s\n", url.c_str());
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
        Serial.printf("[API] Claimed OK, token=%s...\n", result.token.substring(0, 12).c_str());
    } else {
        Serial.printf("[API] Claim failed: HTTP %d\n", code);
        if (code > 0) {
            Serial.println(http.getString());
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

    Serial.printf("[API] GET %s\n", url.c_str());
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
        Serial.printf("[API] /me: mapped=%d station_id=%s type=%s\n",
            info.mapped, info.stationId.c_str(), info.type.c_str());
    } else {
        Serial.printf("[API] /me failed: HTTP %d\n", code);
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
        Serial.printf("[API] Heartbeat failed: HTTP %d\n", code);
    }

    http.end();
    return ok;
}

EventResult apiPostEvent(const String& token, const String& eventId, const String& ts,
                         const String& rfidUid, const String& eventType) {
    HTTPClient http;
    String url = String(SERVER_URL) + "/api/v1/events";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    JsonDocument doc;
    doc["event_id"] = eventId;
    doc["ts"] = ts;
    doc["event_type"] = eventType;

    JsonDocument bundle;
    bundle["rfid_uid"] = rfidUid;
    doc["bundle"] = bundle;

    String body;
    serializeJson(doc, body);

    Serial.printf("[API] POST event: %s %s %s\n", rfidUid.c_str(), eventType.c_str(), eventId.c_str());
    int code = http.POST(body);

    EventResult result;
    if (code == 200) {
        result = EVENT_OK;
    } else if (code == 401) {
        result = EVENT_UNAUTHORIZED;
    } else if (code == 404) {
        result = EVENT_UNKNOWN_BUNDLE;
    } else if (code == 409) {
        String resp = http.getString();
        if (resp.indexOf("station_unmapped") >= 0) {
            result = EVENT_UNMAPPED;
        } else {
            result = EVENT_TYPE_MISMATCH;
        }
    } else if (code == 400) {
        result = EVENT_INVALID;
        Serial.printf("[API] Event invalid: %s\n", http.getString().c_str());
    } else {
        result = EVENT_NETWORK_ERROR;
        Serial.printf("[API] Event network error: HTTP %d\n", code);
    }

    http.end();
    return result;
}
