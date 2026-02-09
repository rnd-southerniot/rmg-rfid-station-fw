#include "led_buzzer.h"
#include "config.h"

static void setLed(bool r, bool g, bool b) {
    digitalWrite(LED_R_PIN, r ? HIGH : LOW);
    digitalWrite(LED_G_PIN, g ? HIGH : LOW);
    digitalWrite(LED_B_PIN, b ? HIGH : LOW);
}

#define BUZZER_CHANNEL 0

static void toneMs(int freq, int ms) {
    ledcWriteTone(BUZZER_CHANNEL, freq);
    delay(ms);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void ledBuzzerInit() {
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    ledOff();

    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void ledOff()    { setLed(false, false, false); }
void ledGreen()  { setLed(false, true, false); }
void ledRed()    { setLed(true, false, false); }
void ledYellow() { setLed(true, true, false); }
void ledBlue()   { setLed(false, false, true); }

void beepSuccess() {
    toneMs(2000, 100);
}

void beepWarning() {
    toneMs(1000, 150);
    delay(80);
    toneMs(1000, 150);
}

void beepError() {
    toneMs(500, 300);
}
