#pragma once

// ── Board selection ───────────────────────────────────────
// Pins and board capabilities live in src/boards/board_<name>.h, chosen by the -DBOARD_*
// build flag of the PlatformIO environment (see platformio.ini and docs/PIN_MAP.md).
#if defined(BOARD_RAK3212)
  #include "boards/board_rak3212.h"
#elif defined(BOARD_ESP32DEV)
  #include "boards/board_esp32dev.h"
#else
  #error "Select a board: build with -DBOARD_ESP32DEV=1 or -DBOARD_RAK3212=1"
#endif

// ── LCD (ILI9341 over SPI) ────────────────────────────────
// Pin config handled by TFT_eSPI build flags in platformio.ini
#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// ── Timing constants ──────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS    60000   // 60 seconds
#define MAPPING_POLL_INTERVAL_MS 300000  // 5 minutes
#define SCAN_DEBOUNCE_MS         3000    // Ignore same UID for 3s
#define SCAN_RESULT_DISPLAY_MS   3000    // Show result for 3s
#define QC_TOUCH_TIMEOUT_MS      10000   // 10s to pick PASS/FAIL
#define WIFI_RETRY_INTERVAL_MS   5000    // 5s between WiFi retries
#define NTP_SERVER               "pool.ntp.org"

// ── Logging ──────────────────────────────────────────────
// 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG
#define LOG_LEVEL   2  // INFO

// ── Firmware version ──────────────────────────────────────
#define FW_VERSION  "0.1.0"
