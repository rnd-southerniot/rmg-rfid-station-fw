---
name: lorawan-fallback-contract
description: The LoRaWAN offline-fallback design of the RAK3212 RFID station — what goes over LoRa and when, the schema-1 payload (fPort 10 scan, fPort 11 heartbeat), the E_<epoch>_<seq> de-duplication rule the ETS backend must implement, the ChirpStack codec and its vector test, bench provisioning with provision_bench.sh, the join/uplink verification lines, and the stale-session trap. Use when touching lora_link/lora_payload, the codec, ChirpStack device profiles, or backend ingestion.
---

# LoRaWAN offline fallback — contract and procedure

Authoritative doc: `docs/LORAWAN_PAYLOAD.md`. Encoder: `src/lora_payload.{h,cpp}` (pure, tested).
Link: `src/lora_link.{h,cpp}` (RadioLib 7.7.1 task on core 0). Codec: `tools/chirpstack/rfid_station_codec.js`.

## Role (decided 2026-09-24)

WiFi/HTTP is primary for claim, mapping, login, heartbeat and events. LoRa carries a **copy** of a
scan event only when its HTTP POST failed (queued for replay anyway) and a heartbeat while the
link is down. Replays never re-send over LoRa. With a joined link, cached mapping and cached
operator session the station keeps scanning without WiFi (offline mode in `main.cpp`).

## Payload (big-endian, ≤ 24 B)

- fPort 10 scan: `schema=1, flags(b0 time_synced b1 qc b2 operator b3 replay), epoch u32, seq u16,
  event_type(1 COMPLETE 2 QC_PASS 3 QC_FAIL), uid_len, uid[], [op_len, op[]]`.
- fPort 11 heartbeat (20 B): `schema, flags, uptime u32, queued_http u16, last_seq u16, dropped u8,
  fw[3], rssi i8, offline_reason, epoch u32`.
- Golden vectors S1/S2/S3/H1 are shared by `pio test -e native` and `node tools/chirpstack/codec_test.js`.
  Change all three places together.

## De-dup rule for the backend

HTTP `event_id = "E_<epoch>_<seq>"` carries the same pair as the frame. Station identity = DevEUI
with `FF FE` removed = the claim MAC (DevEUI is MAC-derived unless `LORA_DEV_EUI` is set). A LoRa
frame creates a provisional event; the HTTP replay with the same seq upgrades it. `seq` is a
persisted u16 (checkpoint 64, +64 at boot), so bound the window in time.

## Radio config (bench-proven on this module family)

`begin(923.2, 125, SF9, CR7, private sync, 10 dBm, pre 8, TCXO 1.8→1.6 V, DC-DC)`,
`setDio2AsRfSwitch(true)`, `AS923` sub-band 0, `setDwellTime(true, 400)`, after join
`setDatarate(3)` + `setADR(false)`; nonces saved after every join attempt, session after every
uplink (NVS `lorawan`). `SPI.begin(5,3,6,-1)` must precede `radio.begin()`.

## Provisioning and verification

```bash
tools/chirpstack/provision_bench.sh 3CDC75FFFE6F85DC     # profile + app + device + key → credentials.h
scripts/flash.sh rak3212
tools/bench/serial_capture.py --reset --seconds 40 --send "lora show" --send "lora hb" --after 32
```

Expect `[LoRa] JOINED AS923 (new session); uplink DR3 (SF9)` then `[LoRa] uplink OK fPort=11 len=20`
and a decoded `object.type = "heartbeat"` on the device's Events tab. Reboot → `session restored`.
Antenna on before any flash with a real key (10 dBm TX).

## Traps

- Device deleted/re-created in ChirpStack → node restores a stale session, logs "uplink OK", server
  logs `No device-session exists`. Fix: `lora clear-session yes` on the console.
- `-5 TX_TIMEOUT` → DR not pinned (SF12 ToA overruns). `-2` at begin → SPI order/pins.
- All-zero AppKey = LoRa disabled at runtime (`LORA_NO_CREDS`), by design for clean clones.
