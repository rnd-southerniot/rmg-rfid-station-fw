# Architecture — RAK3212 port with LoRaWAN offline fallback

Decisions taken 2026-09-24 (Arif): LoRaWAN = **offline fallback for scan events only**; reader in
**UART** mode; display = lcdwiki MSP2834 **with** FT6336G touch. Full plan:
`~/.claude/plans/create-a-plan-to-composed-knuth.md` (approved).

## Multi-board layout
- `platformio.ini`: `[env]` shared + `[tft_fonts]`; `esp32dev` (unchanged values, `-DBOARD_ESP32DEV`),
  `rak3212` (`esp32-s3-devkitc-1`, `qio_opi`, 16 MB, `default_16MB.csv`, USB CDC on boot,
  TFT_eSPI HSPI, RadioLib **7.7.1 exact**), `rak3212-bringup` (Phase-1 sketch only), `native` (Unity).
- `src/config.h` selects `src/boards/board_esp32dev.h` / `board_rak3212.h`; board-independent
  timings stay in `config.h`. TFT pins live only in `platformio.ini`.
- Backends chosen by `build_src_filter`: `rfid_mfrc522.cpp` (esp32dev) vs `rfid_uart.cpp` +
  `rfid_frame.cpp` (rak3212); `lora_link.cpp`, `lora_payload.cpp`, `serial_console.cpp` rak3212 only.
- `lora_link.h` / `serial_console.h` provide **inline no-op stubs** when `BOARD_HAS_LORA` /
  `BOARD_HAS_CONSOLE` are 0, so `main.cpp` has no `#ifdef` for them (only the POST line and the
  PSRAM log are conditional).

## Pure, host-tested modules (no Arduino)
- `rfid_frame.{h,cpp}`: 7941E frame parser + repeat suppression + hex formatter (17 tests).
- `lora_payload.{h,cpp}`: fPort 10/11 encoders, hex helpers (11 tests). Golden vectors are shared
  with `tools/chirpstack/codec_test.js` (10 vectors) and `docs/LORAWAN_PAYLOAD.md`.

## LoRa link (`lora_link.cpp`)
- Radio config = line-for-line port of `siot-lorawan-node/src/lora.cpp` (bench-proven on the
  same module): `begin(923.2,125,SF9,CR7,PRIVATE,10 dBm,pre 8,TCXO 1.8→1.6 V,DC-DC)`,
  `setDio2AsRfSwitch(true)`, `AS923` sub-band 0, `setDwellTime(true,400)`, DR3 + ADR off after
  join, nonces persisted after every join attempt, session after every uplink (NVS `lorawan`),
  DevEUI = MAC with `FF FE` inserted unless `LORA_DEV_EUI` is set.
- FreeRTOS task on core 0 (8 KB stack, prio 1) fed by a 16-entry queue of commands
  (SCAN payload / HEARTBEAT / JOIN / CLEAR_SESSION); status copied under a spinlock.
  Join backoff 10/30/60 s, 300 s after 10 failures. Re-join on 5 consecutive uplink failures or
  `NETWORK_NOT_JOINED`. Queue full → drop the oldest LoRa copy (counted, reported in heartbeat).
- Heartbeat: once after the first join; while the link is down, immediately then every 5 min.

## Application integration (`main.cpp`)
- `event_id` is now `E_<epoch>_<seq>` (both boards); `seq` = `storageNextSeq()` (NVS checkpoint
  every 64, +64 jump-ahead at boot). The LoRa frame carries the same pair → backend de-dup.
- On `EVENT_NETWORK_ERROR`: queue for HTTP replay **and** `loraEnqueueScan()` (only at the first
  failure; replays never re-send over LoRa). Screen says "Queued + LoRa" when joined.
- `noteHttpResult()`: 2 consecutive HTTP failures with WiFi up → link down (SERVER_UNREACHABLE).
- **Offline operating mode**: when `loraIsJoined() && stationId && userJwt` are all present,
  READY keeps running without WiFi (no heartbeat/mapping/flush, scans go straight to queue+LoRa,
  WiFi retried every 5 s); RECONNECTING hands over to READY/LOGIN accordingly; the LOGIN screen
  shows "Offline - login needs WiFi". Identity (token, JWT, mapping) is now restored from NVS
  **before** the WiFi attempt so a no-WiFi boot can use it (also avoids a needless re-claim).
- Status bar shows `LoRa OK / ... / X` (rak3212 only; esp32dev passes `LORA_UI_NONE`).

## Bench console (`serial_console.cpp`, rak3212)
`help`, `sys info`, `sys loop`, `lora show|join|hb|clear-session yes`, `rfid raw on|off`,
`rfid stats`, `queue show`, `ui qc`, `ui touch on|off`. Keys/tokens never printed.
