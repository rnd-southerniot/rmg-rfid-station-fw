# app-rmg-rfid-station-fw

RMG RFID station firmware. Embedded firmware for RFID reader stations used in the RMG (Ready-Made Garment) factory floor ETS (Employee Tracking System).

## Overview

| Attribute | Value |
|-----------|-------|
| Platform  | ESP32 (`esp32dev`) and ESP32-S3 RAK3212 (`rak3212`) |
| Framework | PlatformIO + Arduino (core 2.0.x) |
| Protocol  | HTTP/JSON to the ETS backend; LoRaWAN (AS923-1) offline fallback on the RAK3212 |
| Group     | `app-careflow` |

## Features

- RFID card scan and debounce (MFRC522 on `esp32dev`, dual-frequency UART reader on `rak3212`)
- Station claim / mapping / operator login against the ETS backend
- Offline event queue in NVS with HTTP replay
- LoRaWAN offline fallback (`rak3212`): scan events and a heartbeat go out over LoRa while HTTP is down — contract in [docs/LORAWAN_PAYLOAD.md](docs/LORAWAN_PAYLOAD.md)
- 2.8" ILI9341 display + FT6336 capacitive touch (QC PASS/FAIL), status LED (RGB on `esp32dev`, NeoPixel on `rak3212`) + buzzer feedback
- Power-on self-test, ArduinoOTA updates, bench console over USB (`rak3212`)

## Hardware

Two supported boards; pins are in [docs/PIN_MAP.md](docs/PIN_MAP.md) and `src/boards/`.

| Board | Env | Display / touch | RFID | Radio |
|---|---|---|---|---|
| ESP32 DevKit station PCB | `esp32dev` | ILI9341 SPI + FT6336 I2C | MFRC522 (SPI) | — |
| RAKwireless RAK3212 WisDuo breakout (ESP32-S3 + SX1262) | `rak3212` | lcdwiki 2.8" IPS MSP2834 (ILI9341V + FT6336G) | 125 kHz + 13.56 MHz dual-frequency UART reader (7941E family) | SX1262 LoRaWAN, AS923-1 |

### RAK3212 wiring summary

| MSP2834 pin | GPIO | | Other | GPIO |
|---|---|---|---|---|
| LCD_CS | 12 | | RFID reader TX → | 18 (receive-only; level-shift if 5 V TTL) |
| LCD_RST | 39 | | Buzzer | 1 |
| LCD_RS (DC) | 38 | | NeoPixel DIN | 2 (power the pixel from 3.3 V) |
| SDI (MOSI) | 11 | | spare | 14, 17, 21 |
| SCK | 13 | | | |
| LED (backlight) | 42 | | | |
| SDO (MISO) | 10 | | | |
| CTP_SCL / CTP_SDA / CTP_RST | 40 / 9 / 41 | | | |

The SX1262 is wired inside the module (GPIO 3/5/6/7/8/47/48 + 4); never reuse those, and never
call `Wire.begin()` / `SPI.begin()` without explicit pins on this board (see PIN_MAP.md).

## Getting Started

This project uses **PlatformIO + Arduino framework**. Install the [PlatformIO](https://platformio.org/) CLI or VS Code extension.

```bash
git clone https://github.com/rnd-southerniot/app-rmg-rfid-station-fw
cd app-rmg-rfid-station-fw

# 1. Create credentials header from template
cp include/credentials.h.example include/credentials.h
# Edit include/credentials.h with your WiFi + backend URL + factory code
#   WIFI_SSID, WIFI_PASSWORD
#   SERVER_URL    e.g. http://192.168.1.100:3000  (LAN IP, NOT localhost)
#   FACTORY_CODE  e.g. SOUTHERNIOT-DEMO
#   LORA_JOIN_EUI, LORA_APP_KEY   (rak3212 only; all-zero key = LoRa disabled)

# 2. Build + flash the original station
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -e esp32dev          # 115200

# 3. Build + flash the RAK3212 station (native USB, shows up as /dev/cu.usbmodem*)
pio run -e rak3212
pio run -e rak3212 -t upload
pio device monitor -e rak3212           # dtr/rts are forced low in platformio.ini
#    type `help` on the monitor for the bench console (lora show, rfid raw on, ...)
#    scripted capture WITHOUT resetting the chip: tools/bench/serial_capture.py --seconds 15

# 4. Host-native unit tests (frame parser, LoRa payload) + ChirpStack codec check
pio test -e native
node tools/chirpstack/codec_test.js
```

`include/credentials.h` is gitignored — secrets stay local. The ESP32 cannot reach `localhost` on your dev machine; use the LAN IP (`ipconfig getifaddr en0` on macOS).

Bring-up on a fresh RAK3212 (Phase 1 of the port): `pio run -e rak3212-bringup -t upload` flashes a blink/USB/PSRAM sketch that prints `flash=16777216 psram=8388608` and the MAC-derived DevEUI.

## USB Serial Setup (macOS)

The `esp32dev` boards ship with a **CP2102N** USB-UART bridge (Silicon Labs, VID:PID `10C4:EA60`). Install the VCP driver once:

```bash
brew install --cask silicon-labs-vcp-driver
open "/opt/homebrew/Caskroom/silicon-labs-vcp-driver/6.0.2/Install CP210x VCP Driver.app"
# Step through installer → enter password
# If macOS shows "System Extension Blocked":
#   System Settings → Privacy & Security → Allow → "Silicon Labs"
```

Plug board with a **data-capable USB cable** (charge-only cables silently fail — symptom: nothing in `pio device list`). Verify:

```bash
pio device list
# Expect: /dev/cu.usbserial-XXX  Description: CP2102N USB to UART Bridge Controller
ls /dev/cu.SLAB_USBtoUART /dev/cu.usbserial-*
```

If chip is **CH340** instead (`1A86:7523`), install [WCH driver](https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html) — CP210x driver won't help.

PlatformIO auto-detects the port for `pio run -t upload`. To pin explicitly, add to `platformio.ini`:

```ini
upload_port = /dev/cu.usbserial-140
monitor_port = /dev/cu.usbserial-140
```

The RAK3212 needs no driver: its USB-C is the ESP32-S3's native USB-Serial-JTAG (`303A:1001`,
`/dev/cu.usbmodem*`). Do not let a terminal assert DTR/RTS on it (that holds the chip in reset);
`platformio.ini` already sets `monitor_dtr = 0` / `monitor_rts = 0`.

## Backend API

| Endpoint | Purpose |
|---|---|
| `POST /api/v1/stations/claim` | station registration by MAC + factory code → bearer token |
| `GET /api/v1/station/me` | mapping (station_id, line_id, type) |
| `POST /api/v1/station/heartbeat` | liveness |
| `POST /api/v1/auth/login` | operator badge → user JWT |
| `POST /api/v1/events` | scan events `{event_id: "E_<epoch>_<seq>", ts, event_type, bundle: {rfid_uid}}` |

LoRaWAN uplinks (fPort 10 scan copies, fPort 11 heartbeat) carry the same `(epoch, seq)` as the
HTTP `event_id` so the backend can de-duplicate; see [docs/LORAWAN_PAYLOAD.md](docs/LORAWAN_PAYLOAD.md).

## Related

- [`app-rmg-rfid-ets`](https://github.com/rnd-southerniot/app-rmg-rfid-ets)

## License

MIT
