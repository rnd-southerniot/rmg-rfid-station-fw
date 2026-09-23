---
name: rfid-uart-reader-discovery
description: How to characterise an unknown UART RFID reader module on the RAK3212 station — safety gate on its TX level, baud sweep with `rfid baud`, raw frame capture with `rfid raw on`, telling the ASCII (STX hex CR LF ETX) and binary 7941E frame formats apart, setting the board-header constants, adding the captured bytes as parser golden vectors, and the byte-order check against a badge already enrolled in the ETS backend. Use when a reader module is swapped, prints garbage, or a UID does not match the backend.
---

# UART RFID reader discovery

Parser: `src/rfid_frame.{h,cpp}` (pure, host-tested) with two formats selected by
`RFID_UART_FORMAT` in `src/boards/board_rak3212.h`. Backend glue: `src/rfid_uart.cpp`.

## 0. Safety gate (ESP32-S3 is not 5 V tolerant)

Power the reader (5 V, common GND) with its TX **not** connected. Meter the TX idle level.
≤ 3.3 V → straight to the RX GPIO (18). > 3.6 V → 1.8 kΩ series + 3.3 kΩ to GND (≈ 3.2 V) or
a BSS138 shifter. Record the reading; the current bench module was wired without one (UNKNOWN).

## 1. Is the reader reaching the pin?

`rfid stats` → `rx line GPIO18 is HIGH right now (reader present and idle)`. LOW = unpowered,
wrong pin, or Wiegand mode (D0/D1 instead of TX). The boot POST reads the same probe.

## 2. Find the baud

```bash
tools/bench/serial_capture.py --seconds 60 --after 1 --gap 8 \
  --send "rfid raw on" --send "rfid baud 115200" --send "rfid baud 19200" --send "rfid baud 38400" --send "rfid baud 57600"
```

Tap the badge once per step. Wrong baud = a short burst of garbage that looks the *same on every
tap* (e.g. `49 FE` at 9600 for this module). Right baud = a frame that starts with `02`.

## 3. Read the frame

- **ASCII** (`RFID_FORMAT_ASCII_HEX`, PROVEN for the bench module at 115200):
  `02 34 30 35 30 42 30 34 37 0D 0A 03` = STX "4050B047" CR LF ETX. No type byte, no checksum.
  Even hex-digit count, 2–20 digits.
- **Binary 7941E** (`RFID_FORMAT_BINARY_7941E`, vendor family, ASSUMED): `02 LEN TYPE DATA… BCC 03`,
  LEN = total length, TYPE 0x01 Mifare / 0x02 EM4100, BCC = XOR(LEN..DATA); options
  `RFID_UART_BCC_MODE`, `RFID_UART_ETX`, `RFID_UART_STRIP_MIFARE_PAD`.

Set `RFID_UART_BAUD`, `RFID_UART_FORMAT` (and the binary options if applicable) in the board
header, then add the captured bytes to `test/test_rfid_frame/test_main.cpp` as a golden vector and
run `pio test -e native`.

## 4. Hold behaviour

Hold the badge for 5 s during a capture. One frame → the module emits on card entry only
(bench module: PROVEN). Periodic frames → set `RFID_UART_HOLD_GAP_MS` ≥ 2× the period. Either
way `SCAN_DEBOUNCE_MS` in `config.h` is the second layer.

## 5. Byte order vs the backend

Present a badge that is **already enrolled** in the ETS (its MFRC522 hex is in the admin UI or
in the old esp32dev log `[Main] Scanned UID: …`). Same string → done. Reversed → set
`RFID_UART_REVERSE_MIFARE_UID 1`. Anything else (decimal, truncated, Wiegand-derived) → the
module's output mode is not raw UID; check its configuration or choose another reader.
