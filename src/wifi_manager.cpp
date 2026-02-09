#include "wifi_manager.h"
#include "config.h"
#include "log.h"
#include <WiFi.h>

// credentials.h provides WIFI_SSID and WIFI_PASSWORD
#include "credentials.h"

void wifiInit() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    LOG_I("[WiFi] Connecting");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        LOG_I(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        LOG_E("\n[WiFi] Connection failed, will retry...\n");
    }
}

bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifiReconnect() {
    if (WiFi.status() == WL_CONNECTED) return;
    LOG_D("[WiFi] Reconnecting...\n");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

String wifiGetMac() {
    return WiFi.macAddress(); // Returns "XX:XX:XX:XX:XX:XX" uppercase
}
