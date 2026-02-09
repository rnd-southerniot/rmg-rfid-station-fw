#pragma once

// ── LCD (ILI9341 on VSPI) ─────────────────────────────────
// Pin config handled by TFT_eSPI build flags in platformio.ini
#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// ── Touch (FT6336 on I2C) ─────────────────────────────────
#define TOUCH_SDA   21
#define TOUCH_SCL   22
#define TOUCH_RST   25
#define TOUCH_ADDR  0x38

// ── RFID (MFRC522 on HSPI) ───────────────────────────────
#define RFID_SS     5
#define RFID_SCK    14
#define RFID_MOSI   13
#define RFID_MISO   12
#define RFID_RST    -1   // Not connected

// ── Buzzer (PWM) ──────────────────────────────────────────
#define BUZZER_PIN  33

// ── RGB LED (active low / common cathode) ─────────────────
#define LED_R_PIN   32
#define LED_G_PIN   26
#define LED_B_PIN   27

// ── Timing constants ──────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS    60000   // 60 seconds
#define MAPPING_POLL_INTERVAL_MS 300000  // 5 minutes
#define SCAN_DEBOUNCE_MS         3000    // Ignore same UID for 3s
#define SCAN_RESULT_DISPLAY_MS   3000    // Show result for 3s
#define QC_TOUCH_TIMEOUT_MS      10000   // 10s to pick PASS/FAIL
#define WIFI_RETRY_INTERVAL_MS   5000    // 5s between WiFi retries
#define NTP_SERVER               "pool.ntp.org"

// ── Firmware version ──────────────────────────────────────
#define FW_VERSION  "0.1.0"
