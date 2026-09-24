/*
 * p1_blink.cpp — Phase-1 RAK3212 bring-up sketch (env rak3212-bringup). No application code.
 *
 * Proves: flashing over native USB, USB CDC console, flash/PSRAM sizes (qio_opi), the status
 * LED (NeoPixel) and buzzer pins, and the MAC-derived DevEUI. Expected banner:
 *   [P1] RAK3212 bring-up chip=ESP32-S3 rev=x cores=2 flash=16777216 psram=8388608 mac=.. deveui=..FFFE..
 *
 * Pixel diagnostics (added 2026-09-24 when the pixel on GPIO17 did not light). Console lines:
 *   pix status        pin, driver, GPIO-matrix out signal, output enable, pad read-back
 *   pix pad           route the pin to plain GPIO, drive HIGH then LOW, read the pad back
 *   pix gpio          1 Hz square wave on the pin (DMM/scope check), read-back on every edge
 *   pix core          cycle R/G/B with the core's neopixelWrite() — what the app uses
 *   pix rmt           cycle with a private RMT channel (init result printed)
 *   pix bang          cycle with a cycle-counted bit-bang driver (no RMT involved)
 *   pix pin <n>       move the pixel to GPIO n at runtime (rmt/bang/pad/gpio only)
 *   pix rgb r g b     hold one colour (0-255 each) with the current driver
 *   pix inv on|off    invert the pin in the GPIO matrix — for an external NPN inverter stage
 *                     (NPN base via 1 k from GPIO17, collector to Din with 1 k pull-up to 5 V) that
 *                     gives the pixel a full 5 V data swing; the inversion restores the polarity
 *   pix off           pixel off, cycling stopped
 * Default at boot: `core` on NEOPIXEL_PIN, cycling at ~1 Hz — identical to the Phase-1 gate.
 */
#include <Arduino.h>
#include <esp_system.h>
#include <esp32-hal-rmt.h>
#include <esp_cpu.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_struct.h>
#include <soc/gpio_sig_map.h>
#include "config.h"

#define BUZZER_CHANNEL 0

enum Drv : uint8_t { DRV_CORE, DRV_RMT, DRV_BANG, DRV_GPIO, DRV_NONE };
static const char* drvName[] = {"core", "rmt", "bang", "gpio", "none"};

static uint8_t    pixPin = NEOPIXEL_PIN;
static Drv        drv = DRV_CORE;
static bool       cycling = true;
static uint8_t    ledStep = 0;
static uint32_t   lastLedMs = 0;
static uint32_t   lastBannerMs = 0;
static uint32_t   coreSig = 0;          // GPIO-matrix signal the core's neopixelWrite() routed
static bool       invert = false;       // GPIO-matrix output inversion (external NPN inverter)
static rmt_obj_t* rmtCh = nullptr;
static String     rx;

// ── pad helpers ──────────────────────────────────────────────────────────────────────────
static uint32_t outSel(uint8_t pin)  { return GPIO.func_out_sel_cfg[pin].func_sel; }
static uint32_t outInv(uint8_t pin)  { return GPIO.func_out_sel_cfg[pin].inv_sel; }
static void     applyInvert(uint8_t pin) { GPIO.func_out_sel_cfg[pin].inv_sel = invert ? 1u : 0u; }
static uint32_t outEn(uint8_t pin)   { return pin < 32 ? (GPIO.enable >> pin) & 1u : (GPIO.enable1.val >> (pin - 32)) & 1u; }
static int      padRead(uint8_t pin) { return gpio_get_level((gpio_num_t)pin); }

static void routeToGpio(uint8_t pin) {
    // INPUT_OUTPUT: output driven from GPIO.out (SIG_GPIO_OUT), input buffer on for read-back
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT_OUTPUT);
}

static void status() {
    Serial.printf("[pix] pin=GPIO%u drv=%s cycling=%d out_sel=%lu (gpio=%u rmt0=%u coreSig=%lu) oe=%lu inv=%lu pad=%d\n",
                  pixPin, drvName[drv], (int)cycling, (unsigned long)outSel(pixPin), SIG_GPIO_OUT_IDX,
                  RMT_SIG_OUT0_IDX, (unsigned long)coreSig, (unsigned long)outEn(pixPin), (unsigned long)outInv(pixPin), padRead(pixPin));
}

// ── drivers ──────────────────────────────────────────────────────────────────────────────
static void fillFrame(rmt_data_t* d, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t col[3] = {g, r, b};   // WS2812 order GRB
    int i = 0;
    for (int c = 0; c < 3; c++)
        for (int bit = 7; bit >= 0; bit--, i++) {
            const bool one = col[c] & (1u << bit);
            d[i].level0 = 1; d[i].duration0 = one ? 8 : 4;   // 100 ns ticks: T1H 0.8 / T0H 0.4
            d[i].level1 = 0; d[i].duration1 = one ? 4 : 8;
        }
}

static bool rmtSetup(uint8_t pin) {
    if (rmtCh) { rmtDeinit(rmtCh); rmtCh = nullptr; }
    rmtCh = rmtInit(pin, RMT_TX_MODE, RMT_MEM_64);
    if (!rmtCh) { Serial.printf("[pix] rmtInit(GPIO%u) FAILED\n", pin); return false; }
    const float tick = rmtSetTick(rmtCh, 100);
    Serial.printf("[pix] rmtInit(GPIO%u) ok tick=%.0f ns out_sel=%lu oe=%lu\n", pin, tick,
                  (unsigned long)outSel(pin), (unsigned long)outEn(pin));
    return true;
}

static void IRAM_ATTR bangFrame(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t data = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    const uint32_t mask = 1u << (pin & 31u);
    volatile uint32_t* setReg = pin < 32 ? &GPIO.out_w1ts : &GPIO.out1_w1ts.val;
    volatile uint32_t* clrReg = pin < 32 ? &GPIO.out_w1tc : &GPIO.out1_w1tc.val;
    const uint32_t f = getCpuFrequencyMhz();          // cycles per µs
    const uint32_t t0h = f * 4u / 10u, t1h = f * 8u / 10u, period = f * 125u / 100u;
    portDISABLE_INTERRUPTS();
    for (int i = 23; i >= 0; --i) {
        const uint32_t hi = (data & (1u << i)) ? t1h : t0h;
        const uint32_t t = esp_cpu_get_ccount();
        *setReg = mask;
        while (esp_cpu_get_ccount() - t < hi) {}
        *clrReg = mask;
        while (esp_cpu_get_ccount() - t < period) {}
    }
    portENABLE_INTERRUPTS();
    delayMicroseconds(80);                            // reset latch (>50 µs)
}

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
    switch (drv) {
        case DRV_CORE: neopixelWrite(pixPin, r, g, b); break;
        case DRV_RMT: {
            if (!rmtCh) break;
            rmt_data_t frame[24];
            fillFrame(frame, r, g, b);
            if (!rmtWriteBlocking(rmtCh, frame, 24)) Serial.println("[pix] rmtWriteBlocking FAILED");
            break;
        }
        case DRV_BANG: bangFrame(pixPin, r, g, b); break;
        default: break;
    }
}

static void selectDriver(Drv d) {
    cycling = false;
    drv = d;
    switch (d) {
        case DRV_CORE:
            if (pixPin != NEOPIXEL_PIN) { Serial.println("[pix] core driver is bound to NEOPIXEL_PIN (first init)"); pixPin = NEOPIXEL_PIN; }
            gpio_set_direction((gpio_num_t)pixPin, GPIO_MODE_INPUT_OUTPUT);
            if (coreSig) esp_rom_gpio_connect_out_signal(pixPin, coreSig, false, false);   // undo pad/gpio routing
            cycling = true; break;
        case DRV_RMT:  routeToGpio(pixPin); if (rmtSetup(pixPin)) cycling = true; break;
        case DRV_BANG: routeToGpio(pixPin); cycling = true; break;
        case DRV_GPIO: routeToGpio(pixPin); break;
        default: break;
    }
    applyInvert(pixPin);   // gpio_reset_pin()/re-routing clears the matrix inversion bit
    Serial.printf("[pix] driver=%s\n", drvName[drv]);
    status();
}

static void padTest() {
    routeToGpio(pixPin);
    gpio_set_level((gpio_num_t)pixPin, 1); delayMicroseconds(200); const int h = padRead(pixPin);
    gpio_set_level((gpio_num_t)pixPin, 0); delayMicroseconds(200); const int l = padRead(pixPin);
    Serial.printf("[pix] pad GPIO%u: drive HIGH reads %d, drive LOW reads %d -> %s\n", pixPin, h, l,
                  (h == 1 && l == 0) ? "PAD DRIVES OK" : (h == 0 ? "STUCK LOW (short to GND / heavy load?)" : "STUCK HIGH"));
    drv = DRV_GPIO; cycling = false;
}

// ── console ──────────────────────────────────────────────────────────────────────────────
static void handle(const String& line) {
    if (!line.startsWith("pix")) { Serial.printf("echo: %s\n", line.c_str()); return; }
    char a[8] = {0}; int v1 = -1, v2 = -1, v3 = -1;
    sscanf(line.c_str() + 3, "%7s %d %d %d", a, &v1, &v2, &v3);
    if      (!strcmp(a, "status")) status();
    else if (!strcmp(a, "pad"))    padTest();
    else if (!strcmp(a, "gpio"))   selectDriver(DRV_GPIO);
    else if (!strcmp(a, "core"))   selectDriver(DRV_CORE);
    else if (!strcmp(a, "rmt"))    selectDriver(DRV_RMT);
    else if (!strcmp(a, "bang"))   selectDriver(DRV_BANG);
    else if (!strcmp(a, "inv"))    { invert = line.indexOf(" on") > 0; applyInvert(pixPin); Serial.printf("[pix] GPIO%u matrix inversion %s\n", pixPin, invert ? "ON (use an NPN inverter to 5 V)" : "off"); status(); }
    else if (!strcmp(a, "off"))    { cycling = false; setLed(0, 0, 0); Serial.println("[pix] off"); }
    else if (!strcmp(a, "rgb"))    { cycling = false; setLed((uint8_t)v1, (uint8_t)v2, (uint8_t)v3); Serial.printf("[pix] rgb %d %d %d via %s\n", v1, v2, v3, drvName[drv]); }
    else if (!strcmp(a, "pin") && v1 >= 0 && v1 < 48) {
        if (drv == DRV_CORE) drv = DRV_NONE;
        pixPin = (uint8_t)v1; Serial.printf("[pix] pin=GPIO%u (select rmt/bang/gpio next)\n", pixPin); status();
    }
    else Serial.println("[pix] commands: status pad gpio core rmt bang pin <n> rgb r g b inv on|off off");
}

static void banner() {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    const uint64_t deveui = ((uint64_t)mac[0] << 56) | ((uint64_t)mac[1] << 48) | ((uint64_t)mac[2] << 40) |
                            ((uint64_t)0xFF << 32) | ((uint64_t)0xFE << 24) |
                            ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
    Serial.printf("[P1] RAK3212 bring-up chip=%s rev=%u cores=%u flash=%lu psram=%lu heap=%lu "
                  "mac=%02X:%02X:%02X:%02X:%02X:%02X deveui=%016llX up=%lus pix=GPIO%u/%s\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize(),
                  (unsigned long)ESP.getFreeHeap(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned long long)deveui, (unsigned long)(millis() / 1000u), pixPin, drvName[drv]);
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(5);   // never stall on an absent USB host
#endif
    setLed(0, 0, 0);            // core driver: first call allocates the RMT channel
    coreSig = outSel(pixPin);   // remember which matrix signal the core routed to the pin

    ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ_HZ, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ_HZ);
    delay(100);
    ledcWriteTone(BUZZER_CHANNEL, 0);

    delay(500);
    banner();
    status();
}

void loop() {
    const uint32_t now = millis();

    if (now - lastLedMs >= (drv == DRV_GPIO ? 500u : 333u)) {
        lastLedMs = now;
        ledStep = (uint8_t)((ledStep + 1u) % 3u);
        if (drv == DRV_GPIO) {
            const int lvl = ledStep & 1;
            gpio_set_level((gpio_num_t)pixPin, lvl);
            delayMicroseconds(200);
            Serial.printf("[pix] gpio GPIO%u drive %d reads %d\n", pixPin, lvl, padRead(pixPin));
        } else if (cycling) {
            const uint8_t v = NEOPIXEL_BRIGHTNESS;
            setLed(ledStep == 0u ? v : 0, ledStep == 1u ? v : 0, ledStep == 2u ? v : 0);
        }
    }

    if (now - lastBannerMs >= 5000u) {
        lastBannerMs = now;
        banner();
    }

    while (Serial.available() > 0) {
        const char ch = (char)Serial.read();
        if (ch == '\n') { handle(rx); rx = ""; }
        else if (ch != '\r' && rx.length() < 96u) rx += ch;
    }

    delay(10);
}
