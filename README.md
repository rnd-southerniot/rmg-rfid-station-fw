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

```bash
git clone https://github.com/rnd-southerniot/app-rmg-rfid-station-fw
cd app-rmg-rfid-station-fw
idf.py set-target esp32
idf.py menuconfig   # Set WiFi, MQTT broker, device ID
idf.py build flash
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
