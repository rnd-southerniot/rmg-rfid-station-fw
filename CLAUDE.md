# CLAUDE.md — rmg-rfid-station-fw

> Inherits `~/Developer/projects/firmware/CLAUDE.md` (firmware domain) and `~/.claude/CLAUDE.md` (global).
> This file is the execution contract for the RAK3212 port. Plan of record:
> `~/.claude/plans/create-a-plan-to-composed-knuth.md` (approved 2026-09-24).
> Knowledge: `.planning/knowledge/` (architecture, gotchas, sessions) — read before planning.
> Last updated: 2026-09-24

## 1. What this firmware is

RFID reader stations for the RMG factory-floor ETS. Cooperative state machine in `src/main.cpp`
(BOOT → CLAIMING → CHECK_MAPPING → UNMAPPED/LOGIN → READY → SCANNING/QC_WAIT, RECONNECTING) over
HTTP/JSON to the ETS backend, with an NVS queue for offline replay. The `rak3212` build adds a
LoRaWAN **offline fallback**: scan events that could not be posted are also sent as a compact
fPort-10 uplink, plus a fPort-11 heartbeat while the link is down (`docs/LORAWAN_PAYLOAD.md`).
WiFi/HTTP stays primary for claim, mapping, login, heartbeat and events.

## 2. Targets & toolchain

| | `esp32dev` (original PCB) | `rak3212` (new) |
|---|---|---|
| MCU | ESP32 (Xtensa LX6, 240 MHz) | ESP32-S3 in RAK3112 module (LX7 dual-core, 240 MHz) |
| Memory | 4 MB flash, 320 KB SRAM | 16 MB quad flash, 8 MB **octal** PSRAM (`qio_opi`), 512 KB SRAM |
| Radio | WiFi | WiFi + Semtech SX1262 (module-internal, RadioLib **7.7.1** exact) |
| USB | CP2102N bridge (`/dev/cu.usbserial-*`) | native USB-Serial-JTAG `303A:1001` (`/dev/cu.usbmodem*`) — ASSUMED until Phase 1 |
| Partition | `default.csv` (app0/app1 1.25 MB) | `default_16MB.csv` (app0/app1 6.25 MB, nvs 20 KB) |
| Toolchain | PlatformIO `espressif32@6.7.0` = Arduino core **2.0.16** (`ledcSetup/ledcAttachPin` API), TFT_eSPI 2.5.43, ArduinoJson 7 | same + RadioLib 7.7.1 |
| Host tests | `platform = native`, Unity 2.6.1 (`pio test -e native`) | |

## 3. Pin map summary (authoritative: `docs/PIN_MAP.md`, code: `src/boards/board_<env>.h` + `platformio.ini`)

### rak3212

| Function | GPIO | Notes |
|---|---|---|
| LCD SCLK / MOSI / MISO / CS / RS(DC) / RST | 13 / 11 / 10 / 12 / 38 / 39 | SPI3, TFT_eSPI `USE_HSPI_PORT=1` (mandatory on S3) |
| LCD backlight | 42 | `TFT_BL=42` HIGH — Phase-2 schematic gate (logic input vs raw 80 mA LED) |
| Touch CTP_SDA / SCL / RST | 9 / 40 / 41 | FT6336G @0x38, polled (INT unwired) |
| RFID reader TX → | 18 | UART1 9600 8N1, **receive-only** (`RFID_UART_TX -1`) |
| Buzzer | 1 | LEDC ch 0 |
| NeoPixel DIN | 17 | one WS2812-type pixel, core `neopixelWrite()`; 3.3 V supply works (Arif, prior bench). Wired to 17 by Arif 2026-09-24 (was planned on 2); UART1 leaves 17 alone because `RFID_UART_TX` is -1 |
| SX1262 (internal) | 7/5/3/6/8/47/48 (+4) | NSS/SCK/MISO/MOSI/NRESET/DIO1/BUSY; DIO2 drives the RF switch |
| spare | 2, 14, 21 | 14/21 are ADC-capable |
| reserved | 0, 43/44, 45/46, 19/20 | BOOT, UART0 (fallback console), strapping, USB |

### esp32dev

LCD 18/23/19/15/2/4 (VSPI) · touch 21/22/25 · MFRC522 5/14/13/12 (HSPI) · buzzer 33 · RGB LED 32/26/27.

## 4. Hardware safety gates (RAK3212) — confirm in writing before touching the board

1. **3.3 V logic only.** The ESP32-S3 is not 5 V tolerant. The reader (5 V) TX and any 5 V
   peripheral output get a DMM check first: > 3.6 V idle → resistor divider (1.8 kΩ / 3.3 kΩ) or
   BSS138 before GPIO18.
2. **Never call `Wire.begin()` / `SPI.begin()` without pins** on this board: variant default SDA =
   GPIO8 = SX1262 NRESET; default FSPI pins 10–13 = the LCD bus. `SPI.begin(5,3,6,-1)` must run
   before `radio.begin()` (RadioLib calls a pin-less `begin()` itself).
3. **Antenna connected before any LoRa TX** (Phase 4 onward). TX power is 10 dBm.
4. **Backlight**: do not drive the MSP2834 `LED` pin from GPIO42 until the schematic shows a
   logic input; if it is the raw LED, use an external transistor or tie it to the rail.
5. **Serial capture only with `tools/bench/serial_capture.py`.** The USB-Serial-JTAG peripheral
   resets the chip when the host passes through DTR=0/RTS=1; pyserial's usual "dtr=False,
   rts=False, open" order does exactly that on every open (PROVEN 2026-09-24), and a reopen loop
   then masquerades as a firmware boot loop. The tool pre-asserts both lines, opens, then drops
   RTS before DTR. `pio device monitor` (even with `monitor_dtr/rts = 0`) also reset the board:
   uptime fell from 98 s to 54 s across two monitor opens with no other port activity. Use it
   only when a reset is acceptable.
6. **Flash with `scripts/flash.sh`** (esptool direct, `--connect-attempts 30`): PlatformIO's own
   upload failed twice on this board with "No serial data received" while the app was running.
   **Flash = WRITE only.** No `esptool erase_flash` without an explicit instruction; NVS holds the
   station token, JWT, mapping, event queue, seq and the LoRa session/nonces. Wiping the LoRa
   namespace goes through the console (`lora clear-session yes`), nothing else.
7. Power: USB-only is marginal at peak (WiFi + LoRa TX + 80 mA backlight); use a powered hub or a
   bench 5 V with common GND. 5 V for the display/reader comes from header J3 (ASSUMED, verify).

## 5. Canonical commands

```bash
cp include/credentials.h.example include/credentials.h   # once; gitignored; fill WiFi/server/LoRa keys

pio run -e esp32dev && pio run -e esp32dev -t upload && pio device monitor -e esp32dev
scripts/flash.sh rak3212            # build + esptool flash with 30 connect attempts (pio upload fails to sync on this board)
scripts/flash.sh rak3212-bringup    # Phase 1 sketch
tools/bench/serial_capture.py --reset --seconds 36 --send "sys info" --after 30   # boot log + POST results

pio test -e native                      # 28 Unity tests (frame parser, LoRa payload)
node tools/chirpstack/codec_test.js     # 10 codec vectors
pio device list                         # rak3212 → VID:PID=303A:1001
tools/bench/serial_capture.py --seconds 15 --send ping --after 4   # safe capture (no chip reset)
```

## 6. Resource budgets

| Resource | Budget (domain) | esp32dev (2026-09-24) | rak3212 (2026-09-24) |
|---|---|---|---|
| Flash (app partition) | ≤ 60 % | **82.7 %** (1084497 / 1310720) — pre-existing exception: 1.25 MB OTA slots on a 4 MB part; any growth here needs a size justification | 16.4 % (1075705 / 6553600) |
| RAM (static) | ≤ 70 % | 15.9 % (52160 B) | 16.6 % (54456 B) |
| LoRa task stack | 8 KB, high-water > 1.5 KB free | — | measure with `lora show` (Phase 4) |
| Loop latency | max gap < 50 ms (`sys loop`) | — | measure (Phase 5) |
| esp32dev regression | RAM/Flash delta ≤ 2 KB vs `pre-rak3212`, 0 `src/` warnings | +32 B / +1988 B | — |

## 7. Phases (PASS/FAIL gates; every phase ends with `pio run -e esp32dev` green)

| Phase | Goal | Gate (exact expectation) | Rollback |
|---|---|---|---|
| 0 ✅ | multi-env build, pure modules + tests, docs | `pio test -e native` → `28 test cases: 28 succeeded`; codec `10/10`; esp32dev sizes identical after restructure | `git reset --hard pre-rak3212` |
| 1 ✅ (pixel on GPIO17 since 2026-09-24; R/G/B + beep visual pending) | board bring-up: USB CDC, PSRAM, NeoPixel, buzzer | `pio device list` shows `303A:1001` ✓; banner `[P1] RAK3212 bring-up chip=ESP32-S3 rev=0 cores=2 flash=16777216 psram=8386295 … mac=3C:DC:75:6F:85:DC deveui=3CDC75FFFE6F85DC` ✓ (PSRAM = allocator-usable size of 8 MiB); `ping` → `echo: ping` ✓; pixel cycles R/G/B ~1 Hz + one beep = operator check | revert commit |
| 2 ✅ | MSP2834 display + FT6336G touch | `[Display] ILI9341 RDID4 = 0x9341 (OK)` ✓ (bus/wiring/HSPI PROVEN), `[Touch] FT6336 initialized` + `Touch=OK` ✓, `sys info` → `post LCD=OK(id 0x9341) … Touch=OK LoRa=OK` ✓; bonus `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)` ✓ with no TX (AppKey zero). Operator 2026-09-24: screen confirmed; `ui touch on` → `touch  75,160 -> PASS`, `touch 243,148 -> FAIL` (orientation mapping unchanged) | revert |
| **3 ▶** (frame PROVEN, byte order + hold pending) | reader frame discovery + parser config | `rfid raw on` at 115200: `02 34 30 35 30 42 30 34 37 0D 0A 03` = ASCII "4050B047" ✓ (parser ASCII mode, 8 new tests); still to do: `[Main] Scanned UID: X` **equals the backend's enrolled hex** for the same badge (direct or reversed → `RFID_UART_REVERSE_MIFARE_UID`), reader TX level metered. Hold test ✓: one frame per presentation, none while held 5 s | revert; defaults stay vendor values |
| 4 | SX1262 + OTAA join + heartbeat decoded | `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)`; `[LoRa] JOINED AS923 (new session); uplink DR3 (SF9)` < 10 s; `lora hb` → `uplink OK fPort=11 len=20`; ChirpStack shows `object.type="heartbeat"`; reboot → `session restored (no re-join)` | `lora clear-session yes`; revert |
| 5 | fallback end-to-end + de-dup | backend down: scan → `Queued + LoRa`, ChirpStack `event_id="E_<epoch>_N"`; restore → HTTP replay with the **same** N; AP off: READY stays up, heartbeat `WIFI_DOWN`; 30 min soak, `sys loop` < 50 ms | revert Phase 5 commits |
| 6 | docs + `FW_VERSION 0.2.0` + esp32dev regression on the real unit | 5 POST lines unchanged, scan/queue/replay unchanged; `pio run -e esp32dev` delta ≤ 2 KB | docs-only |

Phase 3 settled the frame (ASCII at 115200, `RFID_UART_FORMAT` / `RFID_UART_BAUD`) and the hold
behaviour (one frame per card entry, no re-emit while held). Still open in `board_rak3212.h`:
`RFID_UART_REVERSE_MIFARE_UID` (byte order vs the MFRC522 enrolments).

## 8. Guardrails

- `include/credentials.h` is gitignored and holds WiFi, server, factory code and the LoRa
  JoinEUI/AppKey. Never print keys/tokens/JWTs (the console redacts). Scan before pushing.
- Branch `feat/rak3212-port`; Conventional Commits with the phase in the body; small commits;
  never force-push; no push without being asked.
- esp32dev must keep building and behaving identically: pins live only in `board_esp32dev.h`,
  new code is compiled out through stubs (`lora_link.h`, `serial_console.h`) or `build_src_filter`.
- Pure modules (`rfid_frame`, `lora_payload`) stay Arduino-free and covered by `pio test -e native`;
  any payload change updates the golden vectors in the tests, `codec_test.js` and
  `docs/LORAWAN_PAYLOAD.md` together.
- Evidence discipline: label PROVEN / ASSUMED / UNKNOWN; the Phase 1–3 gates exist to convert the
  ASSUMED items (native USB, reader frame/level/byte order, backlight drive) into PROVEN.
- Backend contract changes (`E_<epoch>_<seq>`, ChirpStack ingest + de-dup) belong to the ETS
  backend repo; this repo only documents them.

## 9. State

<!-- 2026-09-24: Plan approved (LoRa = offline fallback for scans, UART reader, MSP2834 touch). Phase 0 done on feat/rak3212-port (tag pre-rak3212 = ec547a4): multi-env platformio.ini, board headers, rfid_frame + lora_payload (28 native tests), ChirpStack codec (10 vectors), rfid_uart, lora_link (RadioLib 7.7.1 task), serial console, bring-up sketch, main.cpp fallback + offline mode + E_<epoch>_<seq> ids, docs. esp32dev regression +32 B RAM / +1988 B flash, 0 src warnings. -->
<!-- 2026-09-24 NeoPixel wired: Arif connected the pixel DIN to GPIO17 (not the planned GPIO2). NEOPIXEL_PIN 2 -> 17 in board_rak3212.h; PIN_MAP/CLAUDE/README/knowledge/memory updated (spare now 2, 14, 21). PROVEN safe: GPIO17 is the S3 IOMUX U1TXD, but core 2.0.16 attaches a UART TX pin only when >= 0 (esp32-hal-uart.c:153, :256) and RFID_UART_TX is -1, so UART1 never touches it. rak3212-bringup flashed and running: banner + `ping` echo PASS again on GPIO17; R/G/B cycle + beep = Arif's visual (the sketch is LEFT RUNNING for that; `scripts/flash.sh rak3212` puts the app back). esp32dev 52160 B RAM / 1084497 B flash (header not compiled there; delta vs the §6 figure predates this change). ChirpStack pre-check (read-only): a568b-gw-108 ONLINE (last seen 08:41Z), DevEUI 3cdc75fffe6f85dc NotFound -> Phase 4 provisioning still awaits Arif's go. -->
<!-- 2026-09-24 WRAP-UP: project skills (.claude/skills: rak3212-bench, rfid-uart-reader-discovery, lorawan-fallback-contract, multi-board-build), docs/RUNBOOK.md + docs/ARCHITECTURE.md, knowledge entries (gotchas/reader-and-display-bench, api-contracts/lorawan-payload, devops/knowledge-mcp, architecture/PLAN-rak3212-port copy), sessions/2026-09-24-phase4-HANDOFF.md. Knowledge MCP upstream rmg-rfid-station-knowledge deployed on the gateway (127.0.0.1:8020, prefix rmgrfid, 21st upstream, proxy-config backup .bak-20260924-044442-rmgrfid), verified: check_upstream_health 20/20 healthy, get_handoff + search return content. Resync with tools/sync-knowledge-mcp.sh. NEXT SESSION: start from get_handoff / sessions/2026-09-24-phase4-HANDOFF.md (Phase 4 needs Arif's "go" for 10.10.8.140 provisioning, the antenna, and the badge byte-order answer). -->
<!-- 2026-09-24 Phase 3 (bench): reader wired to GPIO18 (line idles HIGH, POST RFID=OK). Raw dump had to move into the console service (app polls the reader only in LOGIN/READY). Baud sweep: garbage of consistent shape at 9600/38400/57600; at 115200 clean frames 02 34 30 35 30 42 30 34 37 0D 0A 03 = ASCII "4050B047" CR LF ETX. Parser gained an ASCII mode (default for rak3212), board header set to 115200/ASCII. Open: UID byte order vs the ETS enrolment for that badge, re-emit period while held, reader TX level (never metered — operator wired it directly). -->
<!-- 2026-09-24 Phase 2 PASS closed by Arif: screen confirmed (SELF-TEST, boot screens, orientation), touch taps 75,160 -> PASS and 243,148 -> FAIL. Next: Phase 3, needs the reader on 5 V with its TX level measured before it meets GPIO18. -->
<!-- 2026-09-24 Phase 2 host-side PASS (bench): display wired per PIN_MAP; ILI9341 RDID4 read back 0x9341 over MISO (new LCD self-test, rak3212 only), FT6336G answers at 0x38, SX1262 begin OK at TCXO 1.8 V (no TX, key zero), PSRAM 8189 KB, console live; RFID FAIL expected (no reader). Vendor schematic read: LED pin = BSS138 gate w/ 10K pull-up (logic input, floating = ON) -> TFT_BL=42 safe; touch I2C header pull-ups go to module VCC (5 V bus if VCC=5 V, ~0.1 mA clamp) -> prefer module VCC 3.3 V / rework R4,R6 for production. PlatformIO upload failed twice with "No serial data received" while the app ran; esptool with --connect-attempts 30 flashed fine both times. -->
<!-- 2026-09-24 Phase 1 PASS (bench): board /dev/cu.usbmodem1401 = 303A:1001 "USB JTAG/serial debug unit" (native USB PROVEN); banner flash=16777216 psram=8386295 heap=369480 mac=3C:DC:75:6F:85:DC deveui=3CDC75FFFE6F85DC, ping echoed; pixel/beep await Arif's confirmation. pio device monitor (monitor_dtr/rts=0) also resets on open (uptime 98 s -> 54 s across two opens). FOUND: pyserial's default DTR-then-RTS open order resets the S3 on every open (rst:0x15 USB_UART_CHIP_RESET) and a reopen loop looked like a boot loop for 10 s — tools/bench/serial_capture.py holds the port safely. One esptool re-flash attempt failed with "No serial data received" while the app ran (cause UNKNOWN, re-test at the Phase-2 flash). -->
<!-- 2026-09-24: Arif fixed LCD/touch pins (CS 12, RST 39, RS 38, MOSI 11, SCLK 13, LED 42, MISO 10, CTP 40/41/9), reader TX -> GPIO18 receive-only, NeoPixel instead of RGB LED (DIN GPIO2, my pick). Arif: WS2812B has run from 3.3 V on his bench before — no level shifting planned. Next: Phase 1 bring-up on the bench. -->
