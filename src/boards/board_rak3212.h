#pragma once
// ── Board: RAKwireless RAK3212 WisDuo breakout (PlatformIO env `rak3212`) ──────────────
// RAK3112 module: ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM, Semtech SX1262 on internal GPIOs.
// Peripherals: lcdwiki 2.8" MSP2834 (ILI9341V + FT6336G touch), 7941E-family UART RFID reader,
// piezo buzzer, RGB LED. Authoritative table: docs/PIN_MAP.md. LCD pins live in platformio.ini.
//
// HARD RULES (ESP32-S3 Arduino variant defaults are dangerous on this module):
//   * NEVER call Wire.begin() without pins — the variant default SDA is GPIO8 = SX1262 NRESET.
//   * NEVER call SPI.begin() without pins — the variant default FSPI pins 10..13 are the LCD bus.
//   * GPIO0 (BOOT), 43/44 (UART0), 45/46 (strapping), 19/20 (USB) stay untouched.

// ── Touch (FT6336G on I2C, module pins CTP_SDA / CTP_SCL / CTP_RST; CTP_INT unwired) ──
#define TOUCH_SDA   9
#define TOUCH_SCL   40
#define TOUCH_RST   41
#define TOUCH_ADDR  0x38

// ── RFID: 125 kHz + 13.56 MHz dual-frequency UART reader (auto-output) ────────────────
// PROVEN on the bench 2026-09-24: 115200 8N1, ASCII frame  02 "4050B047" 0D 0A 03  (STX, hex
// digits, CR, LF, ETX; no type byte, no checksum). The 9600-baud binary 7941E layout that the
// vendor family documents does NOT apply to this module (kept selectable for other readers).
// Receive-only: the reader's TX feeds GPIO18; nothing is sent to the reader, so no TX pin is
// claimed (HardwareSerial only substitutes the S3 default UART1 pins when BOTH pins are < 0).
#define RFID_BACKEND_UART   1
#define RFID_READER_NAME    "UART ASCII"   // POST label: "RFID (UART ASCII)"
#define RFID_UART_NUM       1
#define RFID_UART_RX        18      // reader TX -> ESP32 (reader TX idle level: UNKNOWN, not yet metered)
#define RFID_UART_TX        -1      // receive-only
#define RFID_UART_BAUD      115200  // PROVEN (clean frames only at this rate)
#define RFID_UART_FORMAT    RFID_FORMAT_ASCII_HEX
#define RFID_UART_INTERBYTE_MS   50   // gap that aborts a partial frame
#define RFID_UART_HOLD_GAP_MS    800  // PROVEN 2026-09-24: the module emits ONE frame per card entry, none while held; 800 ms only guards duplicate bursts
#define RFID_UART_BCC_MODE       0    // binary format only
#define RFID_UART_ETX            0x03 // PROVEN: 0x03 follows CR LF
#define RFID_UART_STRIP_MIFARE_PAD 1  // binary format only
#define RFID_UART_REVERSE_MIFARE_UID 0 // 1 if the Phase 3 byte-order check shows the UID reversed vs MFRC522

// ── Buzzer (LEDC channel 0 / timer 0) ─────────────────────
#define BUZZER_PIN      1
#define BUZZER_FREQ_HZ  2700

// ── Status LED: one WS2812-type NeoPixel (data on GPIO17, driven by the core's neopixelWrite) ──
// Wired to GPIO17 by Arif (2026-09-24; GPIO2 was the original plan). GPIO17 is the S3's IOMUX
// U1TXD, but the reader UART never claims it: RFID_UART_TX is -1 and core 2.0.16 attaches a TX
// pin only when it is >= 0 (esp32-hal-uart.c:153, :256). neopixelWrite() uses RMT via the
// GPIO matrix, so any free pin works.
// Pixel powered from 3.3 V: Arif has run WS2812B this way before (operator-reported, 2026-09-24),
// which also keeps the 3.3 V data signal in spec. 330 R in series with data, 100 nF across the pixel.
// Brightness starts high per bench rule (dim defaults waste bench cycles); lower after Phase 1.
#define LED_BACKEND_NEOPIXEL  1
#define LED_NAME              "NeoPixel"   // POST label: "LED (NeoPixel)"
#define NEOPIXEL_PIN          17
#define NEOPIXEL_BRIGHTNESS   255

// ── LCD self-test: read the ILI9341 ID over MISO (GPIO10) at boot; the module's SDO comes
// straight from the panel at 3.3 V (schematic, lcdwiki MSP2833/MSP2834 2022-12-02).
#define LCD_POST_READ_ID 1

// ── LCD backlight: driven by TFT_eSPI (TFT_BL=42, TFT_BACKLIGHT_ON=HIGH in platformio.ini).
// PROVEN from the vendor schematic: the LED pin drives a BSS138 gate (0 R series, 10 K pull-up
// to 3.3 V) that switches the LED cathodes through 2 R — a logic input; floating = backlight ON.
// NOTE the module's touch I2C lines (CTP_SDA/SCL) carry 10 K pull-ups to the module VCC: with
// VCC = 5 V that is a 5 V bus on GPIO9/40 (0.1 mA clamp current — out of spec, not destructive).
// Preferred: module VCC from 3.3 V, or move R4/R6 to VCC3.3 on a production board.
// If PWM dimming is added later use LEDC channel 2 (timer 1) — channel 1 shares timer 0 with
// the buzzer and ledcWriteTone() on the buzzer would retune the backlight.
#define TFT_BL_PIN  42

// ── SX1262 (module-internal wiring, RAK3112/RAK3312 bench-confirmed) ──────────────────
#define BOARD_HAS_LORA  1
#define LORA_SCK        5
#define LORA_MISO       3
#define LORA_MOSI       6
#define LORA_NSS        7
#define LORA_RST        8
#define LORA_DIO1       47
#define LORA_BUSY       48
#define LORA_ANT_SW     4     // not driven: the SX1262 DIO2 controls the RF switch (proven config)
#define LORA_MIN_UPLINK_GAP_MS      10000UL   // spacing between uplinks (AS923 DR3, 400 ms dwell)
#define LORA_HEARTBEAT_INTERVAL_MS  300000UL  // offline heartbeat period
#define LORA_TX_QUEUE_LEN           16        // pending LoRa copies of scan events
#define LORA_TASK_STACK             8192
#define LORA_TASK_PRIO              1
#define LORA_TASK_CORE              0         // Arduino loop() runs on core 1

// ── Bench console over USB CDC (drop for production builds) ──
#define BOARD_HAS_CONSOLE  1
