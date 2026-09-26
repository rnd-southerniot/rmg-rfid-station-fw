# RUNBOOK.md — bench and field procedures

Boards: `esp32dev` (original station PCB) and `rak3212` (RAK3212 breakout + lcdwiki MSP2834 +
UART dual-frequency reader + SX1262 LoRaWAN fallback). Pins: `PIN_MAP.md`. Contract: `../CLAUDE.md`.

## 1. Build, flash, monitor

| Board | Build | Flash | Monitor |
|---|---|---|---|
| esp32dev | `pio run -e esp32dev` | `scripts/flash.sh esp32dev` (PlatformIO upload over the CP2102N) | `pio device monitor -e esp32dev` |
| rak3212 | `pio run -e rak3212` | `scripts/flash.sh rak3212` (esptool direct, 30 connect attempts, write only) | `tools/bench/serial_capture.py …` — **not** `pio device monitor` (it resets the chip) |
| rak3212 bring-up | | `scripts/flash.sh rak3212-bringup` | same |

Host checks: `pio test -e native` (36 Unity tests), `node tools/chirpstack/codec_test.js` (10 vectors).

### Why the RAK3212 needs its own tools

- The ESP32-S3 USB-Serial-JTAG peripheral resets the chip when the host passes DTR=0/RTS=1.
  pyserial (and therefore `pio device monitor`) applies DTR before RTS on open, so every open
  resets the board and the CDC port drops. `tools/bench/serial_capture.py` pre-asserts both lines,
  opens, then drops RTS before DTR. `--reset` performs one deliberate reset and reopens once.
- PlatformIO's upload failed twice with `No serial data received` while the app was running;
  `scripts/flash.sh` calls esptool with `--connect-attempts 30`. Last resort: hold BOOT (J3 pin 6),
  tap RESET, retry.

## 2. Bench console (rak3212, USB CDC)

Type one command per line and wait for the `>` prompt.

| Command | Use |
|---|---|
| `sys info` | chip, flash/PSRAM, heap, MAC, DevEUI, WiFi, uptime, HTTP queue, seq, **POST results** |
| `sys loop` | max gap between loop passes since the last query (target < 50 ms) |
| `lora show` / `lora join` / `lora hb` / `lora clear-session yes` | link state, force join, heartbeat now, wipe NVS session+nonces (after a device was re-created in ChirpStack) |
| `rfid stats` / `rfid raw on|off` / `rfid baud <n>` | reader counters + live RX level, hex dump of frames (works in every state), re-clock UART1 |
| `queue show` | events pending HTTP replay |
| `ui qc` / `ui touch on|off` | draw the QC screen, echo touch points and which button they hit |

Keys, tokens and JWTs are never printed.

## 3. Phase gates (exact expectations)

| Phase | Gate | Status |
|---|---|---|
| 0 host | `36 test cases: 36 succeeded`; codec `10/10`; esp32dev RAM/Flash identical after restructure | PASS 2026-09-24 |
| 1 bring-up | `pio device list` → `303A:1001`; `[P1] … flash=16777216 psram=8386295 … deveui=…FFFE…`; `ping` echoed; pixel cycles, one beep | PASS except the pixel (**DEFERRED** 2026-09-27): wired to GPIO17 (J5-9) but does not light at VDD 5 V or 3.3 V. `pix status/pad/rmt/bang` in the bring-up sketch prove the pin is routed and driven (RMT sig 81, pad reads back 1/0); remaining suspects are pixel VDD/data level, DIN/DOUT, pixel type, wire |
| 2 display/touch | `[Display] ILI9341 RDID4 = 0x9341 (OK)`; `[Touch] FT6336 initialized`; `ui touch on` → PASS/FAIL hits | PASS 2026-09-24 |
| 3 reader | `@115200 8N1 (ASCII frames), idle line HIGH`; `rfid raw on` → `02 34 30 35 30 42 30 34 37 0D 0A 03` → `frame ok NEW uid=4050B047`; one frame per card entry | frame PASS; byte order vs ETS enrolment pending |
| 4 LoRa | `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)`; `JOINED AS923 (new session); uplink DR3 (SF9)`; `lora hb` → `uplink OK fPort=11 len=20`; decoded heartbeat in ChirpStack; reboot → `session restored` | radio detected; join pending (needs antenna + provisioning) |
| 5 fallback | backend down: `Queued + LoRa`, `[LoRa] uplink OK fPort=10`; restore → HTTP replay with the same `seq`; AP off: READY continues, heartbeat `WIFI_DOWN`; 30 min soak | pending |
| 6 docs + regression | esp32dev POST/scan/queue unchanged on the real unit; delta ≤ 2 KB | pending |

## 4. ChirpStack (bench `10.10.8.140`, tenant `52f14cd4-…`, region `as923_1`)

```bash
tools/chirpstack/provision_bench.sh 3CDC75FFFE6F85DC     # profile rmg-rfid-station + app + device + AppKey → include/credentials.h
scripts/flash.sh rak3212                                 # antenna ON first — 10 dBm TX
tools/bench/serial_capture.py --reset --seconds 40 --send "lora show" --send "lora hb" --after 32
```

Verify on the device's Events tab: join, then an `up` on fPort 11 with `object.type = "heartbeat"`.
Read-only checks with grpcurl (token in `~/.config/siot/chirpstack-dev.env`):
`api.GatewayService/List` (gateway ONLINE), `api.DeviceService/Get {"devEui": …}`.

## 5. Troubleshooting

| Symptom | Cause | Action |
|---|---|---|
| Endless `rst:0x15 (USB_UART_CHIP_RESET)` while "monitoring" | the host resets the chip on every port open | use `serial_capture.py`; never auto-reopen |
| `No serial data received` on upload | esptool cannot sync while the app runs | `scripts/flash.sh` (30 attempts); BOOT+RESET |
| White screen / crash on first draw | TFT_eSPI without `USE_HSPI_PORT=1` on the S3 | keep the flag; check SPI3 wiring 13/11/10/12/38/39 |
| `LCD=FAIL(id 0x0000/0xFFFF)` | MISO (GPIO10) or SPI wiring | check `SDO` → GPIO10 |
| `Touch=FAIL` | FT6336G not answering at 0x38 | CTP_SDA 9 / CTP_SCL 40 / CTP_RST 41; module VCC |
| `RFID=FAIL(0x00)`, `rx line LOW` | no reader signal on GPIO18 | reader power, TX wire, Wiegand vs UART mode |
| same garbage bytes on every tap | wrong baud | `rfid baud <n>` sweep; bench module = 115200 ASCII |
| `[LoRa] radio.begin failed (-2)` | SPI order/pins | `SPI.begin(5,3,6,-1)` before `radio.begin()`; module-internal pins |
| join fails `-1116` / `-6` | keys, region, gateway | DevEUI/AppKey match ChirpStack; gateway ONLINE; antenna |
| `uplink OK` but server logs `No device-session exists` | device re-created on the server, stale session on the node | `lora clear-session yes` |
| `-5 TX_TIMEOUT` | DR not pinned to 3 | check `setDatarate(3)` after join |
| radio resets when touch is initialised | pin-less `Wire.begin()` (default SDA = GPIO8 = NRESET) | pass pins explicitly |

## 6. Rollback

- Firmware: `git revert <sha>` on `feat/rak3212-port`; the pre-port state is tag `pre-rak3212`.
- LoRa session on the node: `lora clear-session yes`. Full NVS (tokens, queue): only with an
  explicit instruction — `esptool erase_flash` is never run by default.
- ChirpStack objects: delete the device, application `rmg-rfid-stations`, profile
  `rmg-rfid-station` in the UI; nothing else on the server is touched.
