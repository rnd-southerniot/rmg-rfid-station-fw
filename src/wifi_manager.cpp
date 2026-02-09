#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

// credentials.h provides WIFI_SSID and WIFI_PASSWORD
#include "credentials.h"

void wifiInit() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("[WiFi] Connecting");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed, will retry...");
    }
}

bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifiReconnect() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

String wifiGetMac() {
    return WiFi.macAddress(); // Returns "XX:XX:XX:XX:XX:XX" uppercase
}
