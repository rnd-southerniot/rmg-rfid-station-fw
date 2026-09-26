---
name: rak3212-bench
description: Bench procedure for the RAK3212 (ESP32-S3 + SX1262) RFID station — flashing over native USB, capturing serial without resetting the chip, the console commands, the phase gates and their exact expected lines, and the traps that already cost time (reset-on-open "boot loop", pio upload not syncing, raw dump silent outside LOGIN/READY). Use when flashing, monitoring, or verifying a phase gate on this board.
---

# RAK3212 bench procedure

Board: RAKwireless RAK3212 breakout (RAK3112 module, ESP32-S3, 16 MB flash, 8 MB octal PSRAM,
SX1262). Native USB-Serial-JTAG: `/dev/cu.usbmodem*`, VID:PID `303A:1001` (PROVEN 2026-09-24).
Pins: `docs/PIN_MAP.md`. Contract and state: `CLAUDE.md`.

## Flash

```bash
scripts/flash.sh rak3212            # pio build + esptool write with --connect-attempts 30
scripts/flash.sh rak3212-bringup    # Phase-1 blink/USB/PSRAM sketch
# bring-up console: `pix status|pad|gpio|core|rmt|bang|pin <n>|rgb r g b|off` — proves the pixel pin is routed/driven (2026-09-24)
```

Do not use `pio run -t upload` for this board: it failed twice with `No serial data received`
while the app was running, and the espressif32 builder ignores `upload_flags`, so the retry count
cannot be raised in `platformio.ini`. The script writes only (bootloader, partitions,
boot_app0, app) and never erases NVS. If esptool still cannot sync, hold BOOT (J3 pin 6 =
GPIO0), tap RESET, retry.

## Capture serial — only with the repo tool

```bash
tools/bench/serial_capture.py --seconds 15                              # hold the port, no reset
tools/bench/serial_capture.py --reset --seconds 36 --send "sys info" --after 30   # boot log + POST
tools/bench/serial_capture.py --seconds 40 --send "ui qc" --send "ui touch on" --after 2
tools/bench/serial_capture.py --seconds 60 --after 1 --gap 6 --send "rfid baud 115200" --send "rfid raw on"
```

Why: the USB-Serial-JTAG peripheral treats DTR=0/RTS=1 as a chip reset. pyserial applies DTR
before RTS inside `open()`, so the common "dtr=False, rts=False, open" recipe (also what
`pio device monitor` does with `monitor_dtr/rts = 0`) resets the S3 on every open, the CDC port
drops, and an auto-reopen loop looks exactly like a firmware boot loop (every reset cause
`USB_UART_CHIP_RESET`, saved PC in app flash). The tool pre-asserts both lines, opens, then drops
RTS before DTR. `--reset` resets once on purpose and reopens once, safely.

## Console (USB CDC, `>` prompt, one command per line)

`help` · `sys info` (chip, PSRAM, MAC, DevEUI, WiFi, uptime, queue, seq, POST results) ·
`sys loop` · `lora show|join|hb|clear-session yes` · `rfid raw on|off` · `rfid stats` ·
`rfid baud <n>` · `queue show` · `ui qc` · `ui touch on|off`. Keys and tokens are never printed.

## Gates (exact lines)

| Phase | PASS looks like |
|---|---|
| 1 bring-up | `[P1] RAK3212 bring-up chip=ESP32-S3 … flash=16777216 psram=8386295 … deveui=3CDC75FFFE6F85DC`, `ping` → `echo: ping`; NeoPixel on GPIO17 DEFERRED 2026-09-27 (dark; `pix status` → `out_sel=81 oe=1`, `pix pad` → `PAD DRIVES OK` prove the pin) |
| 2 display/touch | `[Display] ILI9341 RDID4 = 0x9341 (OK)`, `[Touch] FT6336 initialized`, `post LCD=OK(id 0x9341) … Touch=OK`; `ui touch on` → `touch  75,160 -> PASS`, `touch 243,148 -> FAIL` |
| 3 reader | `[RFID] UART1 reader on RX=18 @115200 8N1 (ASCII frames), idle line HIGH`, `rfid raw on` → `[RFID raw] … 02 34 30 35 30 42 30 34 37 0D 0A 03` then `[RFID] frame ok NEW … uid=4050B047` |
| 4 LoRa | `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)`, `[LoRa] JOINED AS923 (new session); uplink DR3 (SF9)`, `lora hb` → `[LoRa] uplink OK fPort=11 len=20`, decoded heartbeat in ChirpStack |
| 5 fallback | backend down: `Queued + LoRa` + `[LoRa] uplink OK fPort=10`; restore: HTTP replay with the same seq |

Without WiFi credentials the app parks in RECONNECTING after a 20 s attempt; the console,
`rfid raw`, and LoRa still work there (the app polls the reader only in LOGIN/READY, which is
why the raw dump lives in the console service).

## Traps

- "Boot loop" after opening the port → the host reset it (see capture section). Check the reset
  cause: `USB_UART_CHIP_RESET` = host, `TG0WDT`/`RTCWDT`/`SW_CPU_RESET` = firmware.
- White screen or crash on first draw → `USE_HSPI_PORT=1` missing (mandatory on the S3).
- Pin-less `Wire.begin()` → resets the radio (default SDA = GPIO8 = SX1262 NRESET).
- `RFID=FAIL(0x00)` at POST → GPIO18 idles LOW: reader unpowered / wrong pin / Wiegand mode.
- Garbage bytes of the same shape on every tap → wrong baud; sweep with `rfid baud`.
- LEDC: buzzer on channel 0; a PWM backlight must use channel 2, never channel 1.
