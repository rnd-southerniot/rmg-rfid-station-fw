/*
 * p1_blink.cpp — Phase-1 RAK3212 bring-up sketch (env rak3212-bringup). No application code.
 *
 * Proves: flashing over native USB, USB CDC console, flash/PSRAM sizes (qio_opi), the RGB LED
 * and buzzer pins, and the MAC-derived DevEUI. Expected banner:
 *   [P1] RAK3212 bring-up chip=ESP32-S3 rev=x cores=2 flash=16777216 psram=8388608 mac=.. deveui=..FFFE..
 */
#include <Arduino.h>
#include <esp_system.h>
#include "config.h"

#define BUZZER_CHANNEL 0

static uint8_t  ledStep = 0;
static uint32_t lastLedMs = 0;
static uint32_t lastBannerMs = 0;
static String   rx;

static void setLed(bool r, bool g, bool b) {
    digitalWrite(LED_R_PIN, r ? HIGH : LOW);
    digitalWrite(LED_G_PIN, g ? HIGH : LOW);
    digitalWrite(LED_B_PIN, b ? HIGH : LOW);
}

static void banner() {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    const uint64_t deveui = ((uint64_t)mac[0] << 56) | ((uint64_t)mac[1] << 48) | ((uint64_t)mac[2] << 40) |
                            ((uint64_t)0xFF << 32) | ((uint64_t)0xFE << 24) |
                            ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
    Serial.printf("[P1] RAK3212 bring-up chip=%s rev=%u cores=%u flash=%lu psram=%lu heap=%lu "
                  "mac=%02X:%02X:%02X:%02X:%02X:%02X deveui=%016llX up=%lus\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize(),
                  (unsigned long)ESP.getFreeHeap(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned long long)deveui, (unsigned long)(millis() / 1000u));
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(5);   // never stall on an absent USB host
#endif
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    setLed(false, false, false);

    ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ_HZ, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ_HZ);
    delay(100);
    ledcWriteTone(BUZZER_CHANNEL, 0);

    delay(500);
    banner();
}

void loop() {
    const uint32_t now = millis();

    if (now - lastLedMs >= 333u) {
        lastLedMs = now;
        ledStep = (uint8_t)((ledStep + 1u) % 3u);
        setLed(ledStep == 0u, ledStep == 1u, ledStep == 2u);
    }

    if (now - lastBannerMs >= 2000u) {
        lastBannerMs = now;
        banner();
    }

    while (Serial.available() > 0) {
        const char ch = (char)Serial.read();
        if (ch == '\n') {
            Serial.printf("echo: %s\n", rx.c_str());
            rx = "";
        } else if (ch != '\r' && rx.length() < 96u) {
            rx += ch;
        }
    }

    delay(10);
}
