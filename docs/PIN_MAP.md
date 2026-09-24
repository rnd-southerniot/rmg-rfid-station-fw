# PIN_MAP.md — authoritative pin tables

Source of truth for every GPIO the firmware touches. Code reads pins from
`src/boards/board_<env>.h` (peripherals) and `platformio.ini` (TFT_eSPI flags); any change
here must land in those files in the same commit.

## RAK3212 breakout (PlatformIO env `rak3212`)

RAK3112 module: ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM, Semtech SX1262 wired inside the
module. 3.3 V logic, **not 5 V tolerant**. Native USB-C (USB-Serial-JTAG, VID:PID `303A:1001`,
ASSUMED until the Phase-1 gate confirms it).

### Header GPIOs (13 used, 3 spare)

| Function | GPIO | Peripheral pin | Bus / notes |
|---|---|---|---|
| LCD SCLK | 13 | MSP2834 `SCK` | SPI3 (TFT_eSPI `USE_HSPI_PORT`) |
| LCD MOSI | 11 | MSP2834 `SDI(MOSI)` | SPI3 |
| LCD MISO | 10 | MSP2834 `SDO(MISO)` | SPI3 (read-back only) |
| LCD CS | 12 | MSP2834 `LCD_CS` | active low |
| LCD DC | 38 | MSP2834 `LCD_RS` | command/data |
| LCD RST | 39 | MSP2834 `LCD_RST` | active low |
| LCD backlight | 42 | MSP2834 `LED` | `TFT_BL=42`, on = HIGH. **PROVEN (vendor schematic 2022-12-02):** the LED pin drives a BSS138 gate (0 Ω series, 10 kΩ pull-up to 3.3 V) switching the LED cathodes through 2 Ω — a logic/PWM input; floating = backlight ON. |
| Touch SDA | 9 | MSP2834 `CTP_SDA` | Wire (FT6336G @ 0x38). **Schematic:** header-side 10 kΩ pull-up to the module **VCC** (R4) behind a BSS138 shifter → a 5 V bus if VCC = 5 V (0.1 mA clamp, out of spec). Prefer module VCC = 3.3 V, or move R4/R6 to VCC3.3 on a production board. |
| Touch SCL | 40 | MSP2834 `CTP_SCL` | Wire; same 10 kΩ pull-up to VCC (R6) |
| Touch RST | 41 | MSP2834 `CTP_RST` | active low |
| Touch INT | — | MSP2834 `CTP_INT` | unwired; touch is polled |
| SD card | — | MSP2834 `SD_CS` | unwired |
| RFID RX | 18 | reader `TX` | UART1 **115200 8N1** (PROVEN 2026-09-24), receive-only. Frame: `02` + ASCII hex digits + `0D 0A 03`, e.g. `02 "4050B047" 0D 0A 03`; no type byte, no checksum. Reader TX idle level: **UNKNOWN, not metered** (wired directly by the operator); > 3.6 V would need a divider (1.8 kΩ series / 3.3 kΩ to GND) or a BSS138 shifter. |
| Buzzer | 1 | piezo | LEDC channel 0 / timer 0 |
| Status LED | 17 | NeoPixel `DIN` | one WS2812-type pixel via the core's `neopixelWrite()` (RMT, any GPIO). Wired to GPIO17 by Arif 2026-09-24 (GPIO2 was the original plan). GPIO17 is the S3 IOMUX `U1TXD`, but UART1 never claims it: `RFID_UART_TX` is -1 and core 2.0.16 attaches a TX pin only when ≥ 0 (`esp32-hal-uart.c:153`). Power the pixel from **3.3 V** (Arif has run WS2812B from 3.3 V on earlier benches — operator-reported; the datasheet's 3.5 V VIH only applies at 5 V supply). 330 Ω in series with DIN, 100 nF across the pixel. |
| spare | 2, 14, 21 | — | 14 and 21 are ADC-capable (AIN1/AIN0); keep them free for analog use. 2 was the planned NeoPixel pin, now unused |

### Module-internal (do not wire, do not reuse)

| SX1262 | GPIO |
|---|---|
| NSS | 7 |
| SCK | 5 |
| MISO | 3 |
| MOSI | 6 |
| NRESET | 8 |
| BUSY | 48 |
| DIO1 | 47 |
| ANT_SW (RF switch power) | 4 — not driven; the SX1262 DIO2 controls the RF switch |

### Reserved

| GPIO | Why |
|---|---|
| 0 | BOOT strapping |
| 43 / 44 | UART0 TX/RX — fallback console if USB CDC is unavailable |
| 45 / 46 | strapping (VDD_SPI voltage / boot mode) |
| 19 / 20 | USB D- / D+ |
| 26–37 | flash / PSRAM (33–37 unavailable on the 16 MB part) |

### Hard rules on this board

1. **Never call `Wire.begin()` without pins.** The ESP32-S3 Arduino variant default SDA is
   GPIO8, which is the SX1262 NRESET line inside the module. (`variants/esp32s3/pins_arduino.h`)
2. **Never call `SPI.begin()` without pins.** The variant default FSPI pins are GPIO10–13, the
   LCD bus. `SPIClass::begin()` is a no-op once the bus is up, and RadioLib's Arduino HAL calls it
   pin-less, so `lora_link.cpp` calls `SPI.begin(5, 3, 6, -1)` before `radio.begin()`.
3. TFT_eSPI **must** be built with `USE_HSPI_PORT=1` on the ESP32-S3 (its default FSPI register
   mapping crashes on the first write). The display then owns SPI3; the radio uses the global
   `SPI` (SPI2).
4. A PWM backlight goes on LEDC channel 2 (timer 1), never channel 1: channel 1 shares timer 0
   with the buzzer and `ledcWriteTone()` would retune the backlight (`esp32-hal-ledc.c`).

### Power

| Rail | Source | Consumers |
|---|---|---|
| 5 V | USB-C via header J3 pins 1–2 (ASSUMED from the datasheet table; verify on the board) | MSP2834 `VCC` (5 V, onboard level shifting + regulator), RFID reader (5 V, ~30 mA) |
| 3.3 V | module LDO, header J3 `VCC` | logic only |

USB-only power is marginal at peak (WiFi bursts + SX1262 TX + 80 mA backlight): use a powered
hub or a bench 5 V supply with common GND.

## ESP32 DevKit (PlatformIO env `esp32dev`) — original station PCB

| Function | GPIO | Notes |
|---|---|---|
| LCD SCLK / MOSI / MISO / CS / DC / RST | 18 / 23 / 19 / 15 / 2 / 4 | VSPI (TFT_eSPI flags in `platformio.ini`) |
| Touch SDA / SCL / RST | 21 / 22 / 25 | FT6336 @ 0x38 |
| RFID MFRC522 SS / SCK / MOSI / MISO | 5 / 14 / 13 / 12 | HSPI; RST not connected |
| Buzzer | 33 | LEDC channel 0, 2700 Hz |
| LED R / G / B | 32 / 26 / 27 | |
