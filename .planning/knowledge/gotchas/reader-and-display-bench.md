# Bench facts — reader and display on the RAK3212 station (2026-09-24)

| # | Fact | Status | Evidence |
|---|---|---|---|
| 1 | The "125 kHz + 13.56 MHz UART/WG" reader on the bench speaks **115200 8N1, ASCII**: `02` + hex digits + `0D 0A 03`, e.g. `02 34 30 35 30 42 30 34 37 0D 0A 03` = "4050B047". No type byte, no checksum. | PROVEN | `rfid raw on` capture, two identical frames, parsed by the ASCII mode |
| 2 | At 9600 the same tap produced `49 FE`, at 38400 a 5-byte burst, at 57600 a 6-byte burst — consistent garbage per baud is the signature of a wrong baud, not of a broken reader. | PROVEN | baud sweep with `rfid baud` |
| 3 | The reader emits **one frame per card entry** and nothing while the card stays on the antenna (5 s hold). | PROVEN | Arif's hold test |
| 4 | The raw dump was silent until it moved into the console service: the application polls the reader only in LOGIN/READY, and without WiFi the station sits in RECONNECTING. | PROVEN | first two captures empty, fixed in `rfid_uart.cpp` |
| 5 | Reader TX idle level was never metered (wired straight to GPIO18 by the operator). GPIO18 reads HIGH. | UNKNOWN | — |
| 6 | Badge `4050B047`: whether this equals the ETS enrolment (MFRC522 byte order) is not yet known. | UNKNOWN | awaiting the backend value |
| 7 | MSP2834 `LED` pin = 0 Ω → BSS138 gate, 10 kΩ pull-up to 3.3 V, drain → LED cathodes via 2 Ω: logic/PWM input; floating = backlight ON. GPIO42 HIGH is safe. | PROVEN | vendor schematic `2.8inch_SPI_Module_MSP2833_MSP2834_Schematic.pdf` (2022-12-02) |
| 8 | MSP2834 touch lines: BSS138 shifters with 10 kΩ pull-ups to the **module VCC** on the header side → a 5 V bus on GPIO9/40 when VCC = 5 V (≈ 0.1 mA clamp; out of spec, not destructive). TFT signals go through a 74LVC245 at 3.3 V; `SDO` comes straight from the panel. | PROVEN | same schematic |
| 9 | ILI9341 answers `RDID4 = 0x9341` over MISO at 20 MHz read clock on this wiring; FT6336G answers at 0x38. | PROVEN | boot log |
| 10 | The SX1262 answers `radio.begin()` at TCXO 1.8 V with the module-internal pin map, from the Arduino core on the global SPI. | PROVEN | `[LoRa] SX1262 up` (no TX, key zero) |
| 11 | NeoPixel on GPIO17 (header J5-9) does not light, yet the ESP32 side is fine: `neopixelWrite()` routes RMT signal 81 to the pin (oe=1), the pad reads back 1/0 when driven, a private RMT channel and a cycle-counted bit-bang driver both run. A pad read-back proves the pin, not the wire or the pixel. | PROVEN (pin) / UNKNOWN (pixel) | bring-up sketch `pix status/pad/rmt/bang`, 2026-09-24; check pixel VDD vs 3.3 V data, DIN/DOUT, pixel family, 330 R/wire |
