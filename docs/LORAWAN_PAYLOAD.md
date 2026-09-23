# LORAWAN_PAYLOAD.md — LoRaWAN offline-fallback contract (schema 1)

Applies to the `rak3212` build. WiFi/HTTP remains the primary transport for claim, mapping,
operator login, heartbeat and events. LoRaWAN carries **copies** of scan events that could not
be posted over HTTP, plus a heartbeat while the link is down. Encoder: `src/lora_payload.h`.
Decoder for ChirpStack v4: `tools/chirpstack/rfid_station_codec.js`.

## Radio / network parameters

| Item | Value | Provenance |
|---|---|---|
| Region | AS923-1 (RadioLib `AS923`, sub-band 0), 400 ms uplink dwell enforced | bench-proven on the same module (siot-lorawan-node) |
| Activation | OTAA, LoRaWAN 1.0.x (`beginOTAA(joinEUI, devEUI, appKey, appKey)`) | same |
| Data rate | pinned DR3 (SF9BW125) after join, ADR off | same (SF12 overruns the TxDone wait) |
| TX power | 10 dBm | same |
| Uplinks | unconfirmed, ≥ 10 s apart (`LORA_MIN_UPLINK_GAP_MS`), never more than the stack's `timeUntilUplink()` allows | this firmware |
| Downlinks | none defined in schema 1 (`encodeDownlink` returns no bytes) | — |
| Persistence | NVS namespace `lorawan`: `nonces` saved after every join attempt, `session` after every uplink | same |
| DevEUI | `LORA_DEV_EUI` if non-zero, else the base MAC with `FF FE` inserted (`m0 m1 m2 FF FE m3 m4 m5`) — the same MAC the station claims with over HTTP | same |

## fPort 10 — scan event (10 + uid_len [+ 1 + op_len] bytes, ≤ 24)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `schema` | `0x01` |
| 1 | 1 | `flags` | b0 TIME_SYNCED (epoch valid), b1 QC_STATION, b2 OPERATOR_PRESENT, b3 REPLAY, b4–7 reserved 0 |
| 2 | 4 | `epoch` u32 BE | seconds UTC; `0` when NTP never synced (use the gateway rx time) |
| 6 | 2 | `seq` u16 BE | per-station counter, see de-dup rule |
| 8 | 1 | `event_type` | 1 COMPLETE, 2 QC_PASS, 3 QC_FAIL |
| 9 | 1 | `uid_len` | 4..10 |
| 10 | uid_len | `uid` | raw bytes; the HTTP `bundle.rfid_uid` is exactly these bytes as uppercase hex, same order |
| 10+uid_len | 1 | `op_len` | only when OPERATOR_PRESENT |
| 11+uid_len | op_len | `operator_uid` | badge UID of the logged-in operator; omitted (flag clear) if the frame would exceed 24 bytes |

## fPort 11 — offline heartbeat (20 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `schema` = `0x01` |
| 1 | 1 | `flags`: b0 TIME_SYNCED, b1 OPERATOR_LOGGED_IN, b2 STATION_MAPPED, b3 WIFI_ASSOCIATED (WiFi up, backend unreachable) |
| 2 | 4 | `uptime_s` u32 |
| 6 | 2 | `queued_http_events` u16 (events waiting for HTTP replay) |
| 8 | 2 | `last_seq` u16 (lets the server spot gaps) |
| 10 | 1 | `lora_dropped` u8 (LoRa copies discarded since boot, saturating) |
| 11 | 3 | `fw` major, minor, patch |
| 14 | 1 | `wifi_rssi` i8 (0 = unknown) |
| 15 | 1 | `offline_reason`: 0 none, 1 WIFI_DOWN, 2 SERVER_UNREACHABLE, 3 BOOT_NO_WIFI |
| 16 | 4 | `epoch` u32 (device clock, for drift) |

Sent once after the first join of a boot (so the device shows "last seen" during provisioning),
then only while the link is down: immediately on the down-transition and every 5 min
(`LORA_HEARTBEAT_INTERVAL_MS`).

## De-duplication rule (backend contract)

- HTTP `event_id` is `"E_<epoch>_<seq>"` with the **same** `epoch` (u32, 0 if unsynced) and `seq`
  (decimal u16) as the LoRa frame. `seq` is a persisted per-station counter (checkpointed every
  64, jumped ahead by 64 at boot, wraps at 65536), so the de-dup window must be time-bounded
  (30 days is plenty).
- Station identity: DevEUI with bytes 3–4 (`FF FE`) removed = the WiFi MAC = the claim MAC.
- Event identity: (station, seq). A fPort-10 frame creates a *provisional* event. A later HTTP
  `/events` POST whose `event_id` ends in the same `<seq>` for the same station **upgrades** it
  (authoritative timestamp, operator JWT) instead of inserting a duplicate.
- fPort-11 frames update "last seen" and the offline reason; they are not events.
- The firmware sends the LoRa copy only at the **first** HTTP failure of an event; NVS replays
  never generate a second frame.

## Golden vectors (shared by `test/test_lora_payload` and `tools/chirpstack/codec_test.js`)

| Id | fPort | Hex | Decodes to |
|---|---|---|---|
| S1 | 10 | `01016AB13B8004D20104A1B2C3D4` | COMPLETE, epoch 1790000000, seq 1234, uid `A1B2C3D4`, no operator |
| S2 | 10 | `01076AB13B8004D20204A1B2C3D404DEADBEEF` | QC_PASS, QC station, operator `DEADBEEF` |
| S3 | 10 | `01080000000004D2030A10111213141516171819` | QC_FAIL replay, unsynced (epoch 0), 10-byte uid, operator omitted |
| H1 | 11 | `010700000E10000504D2020002 00BD016AB13B80` (spaces for reading only) | uptime 3600, 5 queued, last_seq 1234, dropped 2, fw 0.2.0, rssi −67, WIFI_DOWN, epoch 1790000000 |

Run `pio test -e native` and `node tools/chirpstack/codec_test.js` after any change.

## ChirpStack v4 setup (bench server `10.10.8.140`, tenant `52f14cd4-…`, AS923-1)

1. Device profile **RMG RFID station**: region AS923, MAC version 1.0.4, Regional parameters
   RP002-1.0.3, Class A, ADR disabled (the node pins DR3), payload codec = "Custom JavaScript"
   with the contents of `tools/chirpstack/rfid_station_codec.js`.
2. Application, e.g. **rmg-rfid-stations**.
3. Device: DevEUI from the bench console (`lora show`), JoinEUI as compiled, AppKey = the value
   in `credentials.h` (never commit it). Name the device after the station MAC.
4. If a device is deleted and re-created, the node restores its stale session and reports
   "uplink OK" while the server logs `No device-session exists` — run `lora clear-session yes` on
   the console (or enable "Reset frame counters" is **not** enough).

## Credentials

`include/credentials.h` (gitignored) holds `LORA_DEV_EUI`, `LORA_JOIN_EUI`, `LORA_APP_KEY`. An
all-zero AppKey disables LoRaWAN at runtime (`LORA_NO_CREDS`), so a clean clone builds and boots.
A per-device key store in NVS (`lora creds …`) is a possible follow-up; the shared per-factory key
is the current product assumption.
