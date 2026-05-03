# app-rmg-rfid-station-fw

RMG RFID station firmware. Embedded firmware for RFID reader stations used in the RMG (Ready-Made Garment) factory floor ETS (Employee Tracking System).

## Overview

| Attribute | Value |
|-----------|-------|
| Platform  | ESP32 |
| Framework | ESP-IDF / Arduino |
| Protocol  | MQTT / HTTP |
| Group     | `app-careflow` |

## Features

- RFID card scan and debounce
- MQTT publish on scan event
- HTTP fallback for offline buffering
- LED + buzzer feedback on scan
- OTA firmware update support
- NVS config storage (broker URL, device ID)

## Hardware

- ESP32 DevKit or custom PCB
- RC522 or PN532 RFID reader (SPI/I2C)
- Status LED (RGB)
- Buzzer (passive)

## Wiring (RC522 SPI)

| RC522 | ESP32 |
|-------|-------|
| SDA   | GPIO5 |
| SCK   | GPIO18 |
| MOSI  | GPIO23 |
| MISO  | GPIO19 |
| RST   | GPIO22 |

## Getting Started

This project uses **PlatformIO + Arduino framework** (not ESP-IDF). Install the [PlatformIO](https://platformio.org/) CLI or VS Code extension.

```bash
git clone https://github.com/rnd-southerniot/app-rmg-rfid-station-fw
cd app-rmg-rfid-station-fw

# 1. Create credentials header from template
cp include/credentials.h.example include/credentials.h
# Edit include/credentials.h with your WiFi + backend URL + factory code
#   WIFI_SSID, WIFI_PASSWORD
#   SERVER_URL    e.g. http://192.168.1.100:3000  (LAN IP, NOT localhost)
#   FACTORY_CODE  e.g. SOUTHERNIOT-DEMO

# 2. Build + flash
pio run                  # build
pio run -t upload        # flash over USB
pio device monitor       # serial monitor @ 115200
```

`include/credentials.h` is gitignored — secrets stay local. The ESP32 cannot reach `localhost` on your dev machine; use the LAN IP (`ipconfig getifaddr en0` on macOS).

## USB Serial Setup (macOS)

Boards used here ship with a **CP2102N** USB-UART bridge (Silicon Labs, VID:PID `10C4:EA60`). Install the VCP driver once:

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

## MQTT Events

| Topic | Payload | Description |
|-------|---------|-------------|
| `rfid/{station_id}/scan` | `{"card_id":"...", "ts":...}` | Card scan event |
| `rfid/{station_id}/status` | `{"online":true}` | Heartbeat |

## Related

- [`app-rmg-rfid-ets`](https://github.com/rnd-southerniot/app-rmg-rfid-ets)

## License

MIT
