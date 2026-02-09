#include "ota.h"
#include "config.h"
#include "log.h"
#include "display.h"
#include "led_buzzer.h"
#include <ArduinoOTA.h>

void otaInit(const String& hostname) {
    ArduinoOTA.setHostname(hostname.c_str());

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        LOG_I("[OTA] Start updating %s\n", type.c_str());
        displayBootScreen("OTA Update...");
        ledBlue();
    });

    ArduinoOTA.onEnd([]() {
        LOG_I("\n[OTA] Complete, rebooting...\n");
        displayBootScreen("OTA Done! Rebooting...");
        ledGreen();
        beepSuccess();
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress / (total / 100);
        LOG_D("[OTA] Progress: %u%%\r", pct);
        char buf[32];
        snprintf(buf, sizeof(buf), "Updating... %u%%", pct);
        displayBootScreen(String(buf));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* msg = "Unknown error";
        if (error == OTA_AUTH_ERROR)         msg = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR)   msg = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) msg = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) msg = "Receive Failed";
        else if (error == OTA_END_ERROR)     msg = "End Failed";
        LOG_E("[OTA] Error[%u]: %s\n", error, msg);
        displayError(String("OTA: ") + msg);
        ledRed();
        beepError();
    });

    ArduinoOTA.begin();
    LOG_I("[OTA] Ready, hostname: %s\n", hostname.c_str());
}

void otaHandle() {
    ArduinoOTA.handle();
}
