# API contract — LoRaWAN offline-fallback payload (schema 1)

Authoritative text lives in the repo: `docs/LORAWAN_PAYLOAD.md` (byte tables, golden vectors,
ChirpStack setup) and `src/lora_payload.h` (encoder). This entry records the decisions.

- fPort 10 = scan event copy (≤ 24 B): schema, flags, epoch u32, seq u16, event_type, uid_len, uid,
  optional operator UID. fPort 11 = offline heartbeat (20 B). Big-endian. No downlinks.
- HTTP `event_id = "E_<epoch>_<seq>"` carries the same pair → the ETS backend de-duplicates by
  (station, seq) with a time-bounded window; station = DevEUI minus `FF FE` = claim MAC.
- The LoRa copy is sent only at the first HTTP failure; NVS replays never re-send over LoRa.
- Codec `tools/chirpstack/rfid_station_codec.js`; vectors cross-checked by
  `tools/chirpstack/codec_test.js` and `test/test_lora_payload`.
- Backend side (ChirpStack integration + de-dup) is out of this repo; the backend repo was not
  found under `rnd-southerniot/app-rmg-rfid-ets` on 2026-09-24 — locate it before Phase 5.
