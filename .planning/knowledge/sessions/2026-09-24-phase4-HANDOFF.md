# HANDOFF — next session starts at Phase 4 (LoRaWAN join) · written 2026-09-24

Repo `rnd-southerniot/app-rmg-rfid-station-fw`, branch `feat/rak3212-port` (not pushed), tag
`pre-rak3212` = the state before the port. Contract: `CLAUDE.md`. Plan of record:
`.planning/knowledge/architecture/PLAN-rak3212-port.md`. Bench board: RAK3212 on
`/dev/cu.usbmodem1401`, MAC `3C:DC:75:6F:85:DC`, DevEUI `3CDC75FFFE6F85DC`.

## Where things stand

| Phase | State |
|---|---|
| 0 host | PASS — 36 native tests, 10 codec vectors, esp32dev sizes identical |
| 1 bring-up | PASS — native USB `303A:1001`, PSRAM 8 MB, DevEUI from MAC; NeoPixel wired to **GPIO17** (J5-9) on 2026-09-24 but **does not light**; `pix` diagnostics in the bring-up sketch PROVE the pin is routed (RMT sig 81) and drives (pad reads back 1/0) — physical checks pending: pixel VDD vs 3.3 V data, DIN/DOUT, pixel type, wire |
| 2 display/touch | PASS — ILI9341 ID 0x9341, FT6336G OK, touch hit-boxes confirmed by Arif |
| 3 reader | frame PASS — 115200 ASCII `02 "4050B047" 0D 0A 03`, one frame per card entry; **byte order vs ETS enrolment pending**; reader TX level UNKNOWN |
| 4 LoRa | radio detected (`SX1262 up`, no TX); **provisioning + join not done** |
| 5, 6 | not started |

## Decisions still owed by Arif

1. Badge `4050B047`: the value the ETS backend has for it (same → no change; `47B05040` →
   `RFID_UART_REVERSE_MIFARE_UID 1` in `src/boards/board_rak3212.h`).
2. "go" for creating objects on the bench ChirpStack `10.10.8.140` (ask-before-acting host).
3. Antenna screwed on the RAK3212 before the first flash with a real AppKey (10 dBm TX).
4. Reader TX idle voltage (record only; divider if > 3.6 V).
5. Module VCC for the MSP2834: 3.3 V preferred (touch I2C pull-ups go to VCC).

## Phase 4 — exact steps

```bash
# 0. sanity (read-only): gateway ONLINE, our DevEUI still absent
set -a; source ~/.config/siot/chirpstack-dev.env; set +a
grpcurl -plaintext -H "authorization: Bearer $CS_API_TOKEN" -d "{\"limit\":10,\"tenantId\":\"$CS_TENANT_ID\"}" 10.10.8.140:8080 api.GatewayService/List

# 1. provision (creates profile rmg-rfid-station, app rmg-rfid-stations, device, key → include/credentials.h)
tools/chirpstack/provision_bench.sh 3CDC75FFFE6F85DC

# 2. antenna ON, then flash and watch the join (WiFi creds may stay placeholders: the station parks in RECONNECTING, LoRa still joins)
scripts/flash.sh rak3212
tools/bench/serial_capture.py --reset --seconds 45 --send "lora show" --send "lora hb" --after 34
```

Gate lines: `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)` → `[LoRa] OTAA DevEUI=3CDC75FFFE6F85DC …`
→ `[LoRa] JOINED AS923 (new session); uplink DR3 (SF9)` within ~10 s → `lora hb` →
`[LoRa] uplink OK fPort=11 len=20 fcnt=… rx=0` → ChirpStack device Events: join + `up` with
`object.type = "heartbeat"`, `offline_reason = "BOOT_NO_WIFI"` (no WiFi creds) or `"none"`.
Then reboot (`--reset` capture): `[LoRa] session restored (no re-join)` and fcnt continues.
Then `lora clear-session yes` → a fresh join. `lora show` → task stack high-water > 1.5 KB free.

Failure signatures: `radio.begin failed (-2)` = SPI order/pins; join `-1116`/`-6` = keys/region/
gateway; `-5 TX_TIMEOUT` = DR not pinned; "uplink OK" + server `No device-session exists` =
stale session (`lora clear-session yes`).

## Phase 5 preview

Needs real WiFi + `SERVER_URL` + `FACTORY_CODE` in `include/credentials.h`, the station claimed
and mapped, an operator logged in. Script in `CLAUDE.md` §7 / plan Phase 5. The ETS backend must
implement the `E_<epoch>_<seq>` de-dup (`docs/LORAWAN_PAYLOAD.md`); the backend repo was not
found under `rnd-southerniot/app-rmg-rfid-ets` — ask Arif for its name.

## Tooling to remember

- Flash: `scripts/flash.sh rak3212` (never `pio run -t upload` on this board).
- Serial: `tools/bench/serial_capture.py` only (opening the port any other way resets the S3).
- Console: `help`, `sys info` (POST results), `lora show|join|hb|clear-session yes`, `rfid raw on`,
  `rfid baud <n>`, `ui qc`, `ui touch on`.
- Knowledge MCP: `rmg-rfid-station-knowledge` on the gateway (`get_handoff` returns this file);
  resync with `tools/sync-knowledge-mcp.sh` after doc changes.
