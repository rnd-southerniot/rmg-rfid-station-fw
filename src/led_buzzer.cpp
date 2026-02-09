#include "led_buzzer.h"
#include "config.h"

static void setLed(bool r, bool g, bool b) {
    digitalWrite(LED_R_PIN, r ? HIGH : LOW);
    digitalWrite(LED_G_PIN, g ? HIGH : LOW);
    digitalWrite(LED_B_PIN, b ? HIGH : LOW);
}

static void toneMs(int freq, int ms) {
    ledcWriteTone(0, freq);
    delay(ms);
    ledcWriteTone(0, 0);
}

void ledBuzzerInit() {
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    ledOff();

    ledcAttach(BUZZER_PIN, 2000, 8);
    ledcWriteTone(0, 0);
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
