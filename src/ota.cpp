#include "ota.h"
#include "display.h"
#include "led_buzzer.h"
#include <ArduinoOTA.h>

void otaInit(const String& hostname) {
    ArduinoOTA.setHostname(hostname.c_str());

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.printf("[OTA] Start updating %s\n", type.c_str());
        displayBootScreen("OTA Update...");
        ledBlue();
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Complete, rebooting...");
        displayBootScreen("OTA Done! Rebooting...");
        ledGreen();
        beepSuccess();
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress / (total / 100);
        Serial.printf("[OTA] Progress: %u%%\r", pct);
        char buf[32];
        snprintf(buf, sizeof(buf), "Updating... %u%%", pct);
        displayBootScreen(String(buf));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        const char* msg = "Unknown error";
        if (error == OTA_AUTH_ERROR)         msg = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR)   msg = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) msg = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) msg = "Receive Failed";
        else if (error == OTA_END_ERROR)     msg = "End Failed";
        Serial.println(msg);
        displayError(String("OTA: ") + msg);
        ledRed();
        beepError();
    });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Ready, hostname: %s\n", hostname.c_str());
}

void otaHandle() {
    ArduinoOTA.handle();
}
