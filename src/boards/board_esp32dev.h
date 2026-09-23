#pragma once
// ── Board: ESP32 DevKit (PlatformIO env `esp32dev`) — original RMG RFID station PCB ──
// Pin values moved verbatim from the original src/config.h. Any change here needs a
// matching update in docs/PIN_MAP.md. LCD pins live in platformio.ini (TFT_eSPI flags).

// ── Touch (FT6336 on I2C) ─────────────────────────────────
#define TOUCH_SDA   21
#define TOUCH_SCL   22
#define TOUCH_RST   25
#define TOUCH_ADDR  0x38

// ── RFID (MFRC522 on HSPI) ───────────────────────────────
#define RFID_BACKEND_MFRC522 1
#define RFID_READER_NAME     "MFRC522"   // POST label: "RFID (MFRC522)"
#define RFID_SS     5
#define RFID_SCK    14
#define RFID_MOSI   13
#define RFID_MISO   12
#define RFID_RST    -1   // Not connected

// ── Buzzer (PWM) ──────────────────────────────────────────
#define BUZZER_PIN      33
#define BUZZER_FREQ_HZ  2700  // Resonant frequency for piezo buzzer

// ── RGB LED (active low / common cathode) ─────────────────
#define LED_BACKEND_RGB 1
#define LED_NAME        "R/G/B"   // POST label: "LED (R/G/B)"
#define LED_R_PIN   32
#define LED_G_PIN   26
#define LED_B_PIN   27

// ── LCD self-test: POST keeps reporting "LCD OK" unconditionally (unchanged behaviour) ──
#define LCD_POST_READ_ID 0

// ── Capabilities ──────────────────────────────────────────
#define BOARD_HAS_LORA     0   // no radio: lora_link.h provides inline no-op stubs
#define BOARD_HAS_CONSOLE  0   // no bench console
