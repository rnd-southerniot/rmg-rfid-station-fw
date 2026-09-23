# Plan: port `rmg-rfid-station-fw` to RAK3212 (ESP32-S3 + SX1262) with lcdwiki 2.8" ILI9341 + dual-frequency UART RFID reader

## Context

The repo is a PlatformIO/Arduino ESP32 station firmware (`esp32dev`, Arduino core 2.0.16 via `espressif32@6.7.0`, ~1.8 kLOC): a cooperative state machine in `src/main.cpp` (BOOT → CLAIMING → CHECK_MAPPING → UNMAPPED/LOGIN → READY → SCANNING/QC_WAIT, plus RECONNECTING) talking HTTP/JSON to the ETS backend (`src/api_client.cpp`), MFRC522 over SPI (`src/rfid_reader.cpp`), ILI9341 via TFT_eSPI (`src/display.cpp`), FT6336 touch over I2C (`src/touch.cpp`), an NVS event queue for offline replay (`src/event_queue.cpp`), LED/buzzer, NTP, ArduinoOTA.

Goal: run the same application on a new hardware set, **keeping the existing esp32dev build and behaviour intact**:

| Part | What it is | What changes in firmware |
|---|---|---|
| RAK3212 WisDuo breakout (RAK3112 module) | ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM, SX1262 on internal GPIOs, native USB-C | new PlatformIO env, per-board pin header, USB-CDC console, LoRaWAN link |
| lcdwiki 2.8" IPS SPI MSP2834 | ILI9341V 240×320, 5 V VCC w/ level shifting, FT6336G capacitive touch (I2C 0x38) | pins only; `display.cpp` / `touch.cpp` logic unchanged |
| 125 kHz + 13.56 MHz UART/WG reader (7941E family) | 5 V, auto-emits a framed card ID at 9600 8N1 | new UART reader backend behind the existing `rfid_reader.h` API |

User decisions (confirmed 2026-09-24): LoRaWAN = **offline fallback for scan events** (WiFi/HTTP stays primary for claim, mapping, login, heartbeat, events); reader in **UART** mode; display is the **touch variant**. No knowledge store existed for this project (`$MEMDIR` empty, no `.planning/`), so the facts below were gathered this session.

## Hardware facts (labelled per Evidence Discipline)

| # | Fact | Status | Source |
|---|---|---|---|
| H1 | SX1262 ↔ ESP32-S3: NSS 7, SCK 5, MISO 3, MOSI 6, NRESET 8, BUSY 48, DIO1 47, ANT_SW 4 (internal, not on header) | PROVEN | RAK3212 datasheet table; identical map bench-confirmed in `rak4630-e-ink-claude/firmware/managed_components/siot-lorawan-node/include/siot_lora_pins.h` |
| H2 | Header GPIOs: 1, 2, 9, 10, 11, 12, 13, 14, 17, 18, 21, 38, 39, 40, 41, 42 (+ 43/44 UART0, 45/46 strapping, 0 BOOT). GPIO33–37 unavailable (16 MB flash) | PROVEN | RAK3212 datasheet pin tables J4/J5/J6 |
| H3 | Module boots with `flash_mode=dio/qio` 16 MB + **octal** PSRAM, console on USB-Serial-JTAG | PROVEN | sibling `sdkconfig` (`SPIRAM_MODE_OCT`, `ESP_CONSOLE_USB_SERIAL_JTAG`), bench-run |
| H4 | Proven radio config: `begin(923.2,125,SF9,CR7,PRIVATE,10 dBm,pre 8,TCXO 1.8 V→retry 1.6 V,DC-DC)`, `setDio2AsRfSwitch(true)`, `AS923` sub-band 0, `setDwellTime(true,400)`, after join `setDatarate(3)` + `setADR(false)`, nonces persisted after every join attempt, session after every uplink, DevEUI = MAC with `FF FE` inserted | PROVEN | sibling `lora.cpp:95-207`, RadioLib 7.7.1 |
| H5 | TFT_eSPI 2.5.43 on ESP32-S3 crashes on first write unless built with `-DUSE_HSPI_PORT=1`; it then owns its own `SPIClass(HSPI)` = SPI3, leaving the global `SPI` (= FSPI/SPI2 on S3) free | PROVEN | `my-Claude-buddy/firmware/platformio.ini` (bench 2026-05-03) + `TFT_eSPI_ESP32_S3.c:15`, `SPI.cpp:350` |
| H6 | Arduino S3 variant defaults: **SDA = GPIO8 (= SX1262 NRESET)**, SCL 9, SS 10, MOSI 11, MISO 13, SCK 12. `SPIClass::begin()` is a no-op once begun; RadioLib's Arduino HAL calls `spi->begin()` with **no pins** | PROVEN | `variants/esp32s3/pins_arduino.h:26-32`, `SPI.cpp:71-74`, RadioLib `ArduinoHal.cpp:98-100` |
| H7 | LEDC channel→timer = `(chan/2)%4`; buzzer on channel 0 shares timer 0 with channel 1 | PROVEN | `esp32-hal-ledc.c:64` |
| H8 | `esp32-s3-devkitc-1` board json ships `-DARDUINO_USB_MODE=1`, `ARDUINO_RUNNING_CORE=1`, hwid `0x303A`; `default_16MB.csv` (OTA app0/app1, 20 KB nvs) and the `qio_opi` SDK exist in core 3.20016.0; RadioLib 7.7.1 is in the PIO registry | PROVEN | board json, `tools/partitions/`, `tools/sdk/esp32s3/`, `pio pkg show` |
| H9 | RAK3212 USB-C is native USB-Serial-JTAG (VID:PID 303A:1001); no firmware shipped on the board | ASSUMED | quickstart names no bridge chip; sibling RAK3312 console = USB-Serial-JTAG |
| H10 | lcdwiki MSP2834 header: VCC, GND, LCD_CS, LCD_RST, LCD_RS(DC), SDI, SCK, LED, SDO, CTP_SCL, CTP_RST, CTP_SDA, CTP_INT, SD_CS; 5 V VCC with onboard level conversion; backlight 80 mA | PROVEN (page) / UNKNOWN whether LED pin is a logic input or the raw LED | lcdwiki page; schematic not yet read |
| H11 | Reader frame (7941E family): `02 LEN TYPE ID… BCC`, LEN = total length, TYPE 0x02 = EM4100 (125 kHz) / 0x01 = Mifare (13.56 MHz), BCC = XOR; 9600 8N1; 5 V 30 mA; re-emits while a card is held | ASSUMED | vendor-family docs + gutierrezps/gwiot7941e parser (fixed 10-byte packet, XOR over bytes 1–8, 4 ID bytes MSB-first) |
| H12 | Reader TX logic level (5 V TTL?) and UID byte order vs MFRC522 | UNKNOWN | settled at the Phase 3 bench gate |
| H13 | J3 header exposes USB 5 V (pins 1–2) and 3.3 V | ASSUMED (datasheet table) | verify on the board |

## Pin map — RAK3212 (LCD/touch assignment fixed by Arif 2026-09-24; all 16 header GPIOs used, none double-booked)

| Function | GPIO | Bus |
|---|---|---|
| LCD SCLK / MOSI / MISO / CS / RS(DC) / RST | 13 / 11 / 10 / 12 / 38 / 39 | SPI3 (TFT_eSPI `USE_HSPI_PORT`) |
| LCD backlight (module pin LED) | 42 (`TFT_BL=42`, `TFT_BACKLIGHT_ON=HIGH`; PWM dimming later on LEDC ch 2, never ch 1 — H7). Phase 2 gate confirms the module's LED pin is a logic input, not the raw 80 mA LED | — |
| Touch CTP_SDA / CTP_SCL / CTP_RST | 9 / 40 / 41 (CTP_INT unwired, polled as today) | Wire |
| RFID reader RX (reader TX→) / TX | 14 / 21 (21 optional, unwired for auto-output) | UART1 |
| Buzzer | 1 (LEDC ch 0) | — |
| LED R / G / B | 17 / 18 / 2 | — |
| SX1262 | 7 / 5 / 3 / 6 / 8 / 47 / 48 (+4) | SPI2 = global `SPI` |
| Reserved | 0 BOOT, 43/44 UART0 (fallback console), 45/46 strapping, 19/20 USB | — |

Two hard rules go into the board header and `docs/PIN_MAP.md`: **never call `Wire.begin()` or `SPI.begin()` without explicit pins** on this board (H6: pin-less `Wire.begin()` toggles the radio reset; pin-less `SPI.begin()` would attach FSPI to the LCD pins 10–13).

## Architecture (minimal diff, esp32dev unchanged)

### 1. `platformio.ini` — three firmware envs + native tests
- `[env]` shared: `framework = arduino`, `monitor_speed = 115200`, `lib_deps = ArduinoJson@^7, TFT_eSPI@^2.5.43`; a `[tft_fonts]` block holding today's `LOAD_*`/`SMOOTH_FONT` flags verbatim.
- `[env:esp32dev]`: **all current values unchanged** + `-DBOARD_ESP32DEV=1`, `lib_deps += MFRC522@^1.4.11`, `build_src_filter = +<*> -<rfid_uart.cpp> -<rfid_frame.cpp> -<lora_link.cpp> -<lora_payload.cpp> -<serial_console.cpp> -<bringup/>`.
- `[env:rak3212]`: `board = esp32-s3-devkitc-1`, `board_build.mcu = esp32s3`, `board_build.flash_mode = qio`, `board_build.arduino.memory_type = qio_opi`, `board_build.f_flash = 80000000L`, `board_build.flash_size = 16MB`, `board_upload.flash_size = 16MB`, `board_build.partitions = default_16MB.csv`, `monitor_dtr = 0`, `monitor_rts = 0`, `monitor_filters = esp32_exception_decoder`, `lib_deps += jgromes/RadioLib@7.7.1` (exact pin = proven version), `build_src_filter = +<*> -<rfid_mfrc522.cpp> -<bringup/>`, flags `-DBOARD_RAK3212=1 -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DCORE_DEBUG_LEVEL=0 -DUSER_SETUP_LOADED=1 -DUSE_HSPI_PORT=1 -DILI9341_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 -DTFT_MISO=10 -DTFT_MOSI=11 -DTFT_SCLK=13 -DTFT_CS=12 -DTFT_DC=38 -DTFT_RST=39 -DTFT_BL=42 -DTFT_BACKLIGHT_ON=HIGH -DSPI_FREQUENCY=40000000 -DSPI_READ_FREQUENCY=20000000` + fonts.
- `[env:rak3212-bringup]`: `extends = env:rak3212`, `build_src_filter = -<*> +<bringup/p1_blink.cpp>`.
- `[env:native]`: `platform = native`, `test_framework = unity`, `test_build_src = yes`, `build_src_filter = -<*> +<rfid_frame.cpp> +<lora_payload.cpp>`, `-std=gnu++17 -DUNIT_TEST`. (`platform = native` is installed; the pattern exists in `scl-ets-qc-app/device-backend/platformio.ini`.)
- `[platformio] default_envs = esp32dev, rak3212`.

### 2. Per-board headers
`src/config.h` keeps board-independent values (`LCD_WIDTH/HEIGHT`, timings, `NTP_SERVER`, `LOG_LEVEL`, `FW_VERSION`) and selects `src/boards/board_esp32dev.h` (today's pin block from `config.h:8-28` moved **verbatim** + `RFID_BACKEND_MFRC522 1`, `RFID_READER_NAME "MFRC522"`) or `src/boards/board_rak3212.h` (pin map above: `TOUCH_SDA 9`, `TOUCH_SCL 40`, `TOUCH_RST 41`, `RFID_UART_RX 14`, `RFID_UART_TX 21`, `BUZZER_PIN 1`, `LED_R_PIN 17`, `LED_G_PIN 18`, `LED_B_PIN 2`, `TFT_BL_PIN 42` + `RFID_BACKEND_UART 1`, `RFID_READER_NAME "UART 7941E"`, `RFID_UART_NUM 1`, `RFID_UART_BAUD 9600`, `RFID_UART_INTERBYTE_MS 50`, `RFID_UART_HOLD_GAP_MS 800`, `RFID_UART_REVERSE_MIFARE_UID 0`, `BOARD_HAS_LORA 1`, `LORA_*` pins, `LORA_MIN_UPLINK_GAP_MS 10000`, `LORA_HEARTBEAT_INTERVAL_MS 300000`, `LORA_TX_QUEUE_LEN 16`, `BOARD_HAS_CONSOLE 1`); `#error` if neither flag is set. TFT pins stay in `platformio.ini` (TFT_eSPI needs them in its own TUs).

### 3. RFID backend (API in `src/rfid_reader.h` unchanged)
- `git mv src/rfid_reader.cpp src/rfid_mfrc522.cpp` (no content change).
- `src/rfid_frame.{h,cpp}` — **pure C-style parser, no Arduino includes**: byte-wise state machine `02 LEN TYPE DATA… BCC` with `6 ≤ LEN ≤ 14`, configurable `bcc_mode` (XOR LEN..DATA default; XOR-all; none, for discovery), inter-byte timeout resync, repeat suppression (same UID within `hold_gap_ms` → `RFID_EVT_REPEAT`, mimicking MFRC522 "halted card is not re-read"), counters, and `rfid_uid_to_hex(uid, len, reverse, out)` producing the **same uppercase-hex-no-separator format as `rfid_mfrc522.cpp:36-42`**.
- `src/rfid_uart.cpp` — `HardwareSerial(RFID_UART_NUM)`, `setRxBufferSize(256)` before `begin(9600, SERIAL_8N1, RX, TX)`; `rfidCardPresent()` drains ≤64 bytes per call through the parser and latches one UID; `rfidReadUid()` returns-and-clears; `rfidGetVersion()` = honest presence probe for a passive module: bit0 = RX line idled HIGH at init (reader powered/driving), bit1 = a valid frame seen since boot; 0x00 = absent. `handleBoot`'s existing `ver != 0x00 && ver != 0xFF` test then reads "reader present" with no main.cpp change; POST label uses `RFID_READER_NAME`.
- `SCAN_DEBOUNCE_MS` in `main.cpp` stays as the second layer.

### 4. LoRa link — `src/lora_link.{h,cpp}` (rak3212 only; inline no-op stubs when `!BOARD_HAS_LORA`, so `main.cpp` has zero `#ifdef`s for it)
- API: `loraInit()` (sync `SPI.begin(5,3,6,-1)` + `radio.begin` probe ≈100 ms, then starts the task), `loraService()` (cheap loop pump for log lines), `loraEnqueueScan(const LoraScanEvent&)`, `loraSetLinkDown(bool, reason)`, `loraRequestJoin()`, `loraClearSession()`, `loraGetStatus()`, `loraIsJoined()`, `loraUiState()`.
- **Dedicated FreeRTOS task on core 0** (Arduino loop is pinned to core 1), prio 1, 8 KB stack, fed by a 16-entry `xQueue` of fixed-size commands (SCAN, HEARTBEAT_NOW, JOIN, CLEAR_SESSION); status copied out under a spinlock. Justification: `sendReceive()` blocks ToA + RX1 1 s + RX2 2 s ≈ 2.4–3.3 s and `activateOTAA()` ≈ 6–7 s; inline calls would freeze touch/RFID/OTA (UART RX overruns after ~270 ms undrained). All RadioLib + `Preferences("lorawan")` calls live on the task. Display (SPI3) and radio (SPI2) are separate peripherals, so no bus lock.
- Join FSM = H4 ported line-for-line onto the Arduino HAL: creds from `credentials.h` (`LORA_DEV_EUI` 0 → MAC-derived, `LORA_JOIN_EUI`, `LORA_APP_KEY`; all-zero key → `LORA_NO_CREDS`, radio idle, clean clone still boots), `beginOTAA(joinEUI, devEUI, appKey, appKey)`, restore `nonces`/`session` blobs, `activateOTAA()`, persist nonces every attempt, on `NEW_SESSION`/`SESSION_RESTORED` persist session + DR3 + ADR off; backoff 10/30/60 s (300 s after 10 straight failures). Uplinks: `max(LORA_MIN_UPLINK_GAP_MS, node.timeUntilUplink())` spacing, `len ≤ getMaxPayloadLen()`, `sendReceive(buf, len, fPort, false)`, persist session; on `NETWORK_NOT_JOINED` or 5 consecutive failures re-join. `CLEAR_SESSION` = `node.clearSession()` + remove both blobs + re-join (fix for the known "device re-created in ChirpStack → stale session, server logs `No device-session exists`" gotcha).
- Heartbeat policy: one after first join (shows "last seen" during provisioning); while `linkDown`, one immediately then every 5 min; none while online. Queue full → drop oldest LoRa copy (`dropped++`, reported in heartbeat); the NVS HTTP queue (50) remains the durable store.
- `include/credentials.h.example` gains `LORA_DEV_EUI 0x0ULL`, `LORA_JOIN_EUI 0x0ULL`, `LORA_APP_KEY {16×0x00}` with the comment that DevEUI 0 = derive from the WiFi MAC so the backend maps DevEUI ↔ MAC ↔ station with no extra data.

### 5. Payload contract (big-endian, schema 1, ≤ 24 bytes) — `src/lora_payload.{h,cpp}` (pure, native-tested) + `tools/chirpstack/rfid_station_codec.js`
- **fPort 10 scan**: `[0]` schema=1, `[1]` flags (b0 TIME_SYNCED, b1 QC_STATION, b2 OPERATOR_PRESENT, b3 REPLAY), `[2..5]` epoch u32 (0 if never synced), `[6..7]` seq u16, `[8]` event_type (1 COMPLETE, 2 QC_PASS, 3 QC_FAIL), `[9]` uid_len 4..10, `[10..]` uid bytes (= the HTTP `bundle.rfid_uid` hex, same order), optional `op_len` + operator badge bytes if it still fits in 24 (else b2 cleared).
- **fPort 11 offline heartbeat (20 B)**: schema, flags (b0 TIME_SYNCED, b1 OPERATOR_LOGGED_IN, b2 STATION_MAPPED, b3 WIFI_ASSOCIATED), uptime_s u32, queued_http_events u16, last_seq u16, lora_dropped u8, fw major/minor/patch, wifi_rssi i8, offline_reason (1 WIFI_DOWN, 2 SERVER_UNREACHABLE, 3 BOOT_NO_WIFI), epoch u32.
- **De-dup rule** (contract for the separate backend repo): `generateEventId()` becomes `"E_<epoch>_<seq>"` using the same epoch/seq as the frame; `seq` = persisted u16 in Preferences `rfid-station`/`seq` with jump-ahead 64 at boot (`storageNextSeq()`); station identity = DevEUI with `FF FE` removed = claim MAC. A LoRa frame creates a *provisional* event; the later HTTP replay with the same `E_*_<seq>` upgrades it. `event_queue.cpp` needs no change (it already stores `eventId`). Codec `decodeUplink` emits `event_id`, `rfid_uid`, `operator_uid`, `event_type`, plus the heartbeat fields; `tools/chirpstack/codec_test.js` (plain Node) replays the same golden vectors the native test asserts.

### 6. `main.cpp` hooks (~40 lines, no rewrite; all compile to nothing on esp32dev via stubs)
- includes: `lora_link.h`, `serial_console.h`; `setup()`: `Serial.setTxTimeoutMs(0)` under `ARDUINO_USB_CDC_ON_BOOT` (HWCDC write blocks 100 ms/write with no host attached).
- `handleBoot()`: `loraInit()` → POST line 5 `"LoRa (SX1262)"` (rak3212 only); POST RFID label from `RFID_READER_NAME`; `consoleInit()`; WiFi-fail branch → `loraSetLinkDown(true, BOOT_NO_WIFI)`.
- `generateEventId()` → `E_<ntpGetEpoch()>_<storageNextSeq()>`; `ntp_sync` gains non-blocking `ntpGetEpoch()` (`getLocalTime(&t, 0)`); `storage` gains `storageNextSeq()`.
- `handleLogin()` LOGIN_OK: remember `operatorUid` (cleared wherever `userJwt = ""`).
- heartbeat call sites: `noteHttpResult(ok)` → 2 consecutive failures = `loraSetLinkDown(true, SERVER_UNREACHABLE)`, success = link up.
- `handleEventResult()` `EVENT_NETWORK_ERROR`: after the existing `eventQueuePush`, build `LoraScanEvent` (`lora_hex_to_bytes(uid)`, type enum, QC flag from `stationType == "qc"`, operator bytes) → `loraEnqueueScan()`; message `"Queued + LoRa"` when `loraIsJoined()` (stub false → text unchanged on esp32dev).
- `flushEventQueue()` unchanged (LoRa copy sent at first failure only → no duplicate frames).
- `displayStatusBar(..., loraUiState())` via a **defaulted** 4th parameter (`display.cpp` draws `LoRa ok/…/X` only when ≠ NONE); capture `WiFi.RSSI()` there for the heartbeat.
- `handleReconnecting()`: `loraSetLinkDown(true, WIFI_DOWN)` on entry, link up after reconnect.
- **Offline operating mode (needed for the chosen role to matter; esp32dev unaffected because the stub makes it false):** today a WiFi drop parks the station in RECONNECTING and no scans happen. Add `offlineModeAllowed() = loraIsJoined() && !stationId.isEmpty() && !userJwt.isEmpty()`; when true, `handleReady()`/`handleQcWait()` keep running with WiFi down: HTTP heartbeat + mapping poll skipped, scans go straight to `eventQueuePush` + `loraEnqueueScan` (no HTTP attempt), status bar shows `WiFi X / LoRa ok`, and `wifiReconnect()` is retried every `WIFI_RETRY_INTERVAL_MS` from READY; when WiFi returns the normal flush path runs. No cached JWT → LOGIN screen with an "offline, login unavailable" line (login needs `/auth/login`). Inactivity logout unchanged.
- `loop()`: `loraService(); consoleService();` beside `otaHandle()`.

### 7. Bench console — `src/serial_console.{h,cpp}` (`BOARD_HAS_CONSOLE` only)
Non-blocking line reader on `Serial` (≤ 96 chars, `>` prompt). Commands: `help`, `sys info`, `sys loop` (max loop latency), `lora show` (keys **redacted**), `lora join`, `lora hb`, `lora clear-session yes` (destructive → literal `yes`), `rfid raw on|off`, `rfid stats`, `queue show`, `ui qc` (bench-only screen force). Never prints keys/tokens/JWTs; drop the flag for production. USB-Serial-JTAG gotcha: 64-byte HW FIFO → 256-byte queue drained once per loop pass; one command per write, wait for the prompt (bench-capture ritual: `dtr=rts=False` before open, boot wait, prime `\n`, auto-detect `/dev/cu.usbmodem*`).

## Files

Create: `src/boards/board_esp32dev.h`, `src/boards/board_rak3212.h`, `src/rfid_frame.{h,cpp}`, `src/rfid_uart.cpp`, `src/lora_payload.{h,cpp}`, `src/lora_link.{h,cpp}`, `src/serial_console.{h,cpp}`, `src/bringup/p1_blink.cpp`, `test/test_rfid_frame/test_main.cpp`, `test/test_lora_payload/test_main.cpp`, `tools/chirpstack/rfid_station_codec.js`, `tools/chirpstack/codec_test.js`, `docs/PIN_MAP.md`, `docs/LORAWAN_PAYLOAD.md`, `.planning/knowledge/` (gotchas H5–H7, H11–H12 outcomes, session log) + `$MEMDIR` entries.

Modify: `platformio.ini`, `src/config.h`, `src/rfid_reader.cpp` → `git mv` to `src/rfid_mfrc522.cpp`, `src/main.cpp` (§6), `src/display.{h,cpp}` (defaulted param), `src/ntp_sync.{h,cpp}` (`ntpGetEpoch`), `src/storage.{h,cpp}` (`storageNextSeq`), `include/credentials.h.example`, `README.md` (RAK3212 section; drop stale MQTT/RC522 text), `FW_VERSION` → `0.2.0` in Phase 6.

Reused as-is: `LOG_*` (`src/log.h`), Preferences pattern (`storage.cpp`, `event_queue.cpp`), screen-tracking pattern (`display.cpp:19-22`), `ntpGetIsoTimestamp()`, `touchGetPoint()`/`touchCheckQcButton()`, `apiPostEvent()` result handling, sibling `lora.cpp` as the radio reference.

## Phases (branch `feat/rak3212-port` from `ec547a4`; tag `pre-rak3212` first; one small commit per bullet; every phase ends with `pio run -e esp32dev` green)

### Phase 0 — repo prep + native tests (no hardware)
Steps: record esp32dev baseline (`pio run -e esp32dev` RAM/Flash bytes, keep `compile_commands.json` copy); restructure `platformio.ini`; board headers + `config.h` selector; `git mv` MFRC522 backend; pure `rfid_frame` + `lora_payload` with Unity tests + golden vectors; codec + `codec_test.js`; `docs/PIN_MAP.md`, `docs/LORAWAN_PAYLOAD.md`; credentials template; knowledge capture.
Gate: `pio test -e native` → `N test cases: N succeeded`, 0 failed; `node tools/chirpstack/codec_test.js` → all vectors OK; `pio run -e esp32dev` → **identical** RAM/Flash bytes and 0 `src/` warnings; `pio run -e rak3212` compiles clean (RadioLib 7.7.1 + TFT_eSPI HSPI). Rollback: `git reset --hard pre-rak3212`.

### Phase 1 — RAK3212 bring-up (blink + USB CDC + PSRAM)
Entry: board on USB-C, only RGB LED + buzzer wired, 3.3 V logic confirmed. `src/bringup/p1_blink.cpp`: cycle LED 17/18/2, one beep on GPIO1, banner every 2 s (chip, flash size, PSRAM size, MAC, DevEUI-from-MAC), echo lines.
Gate: `pio device list` shows `303A:1001` (settles H9); `pio run -e rak3212-bringup -t upload && pio device monitor` → `[P1] … flash=16777216 psram=8388608 mac=… deveui=…FFFE…`, LEDs cycling, beep, `ping` echoed. `psram=0` or boot loop = FAIL (memory_type). Rollback: revert commit.

### Phase 2 — display + touch
Entry: Phase 1 PASS; **schematic gate on the MSP2834 LED pin** before driving it from GPIO42 (logic input to an onboard transistor → `-DTFT_BL=42 -DTFT_BACKLIGHT_ON=HIGH` as planned; raw 80 mA LED → do not source it from the GPIO: add an external NPN/MOSFET switched by GPIO42, or tie LED to the rail and set `TFT_BL=-1`); VCC 5 V, SD_CS + CTP_INT unwired. Wiring per the pin map: CS 12, RST 39, RS/DC 38, MOSI 11, SCLK 13, LED 42, MISO 10, CTP_SCL 40, CTP_RST 41, CTP_SDA 9.
Gate: serial `[Display] Initialized (320x240 landscape)`, `[Touch] FT6336 initialized`, `[POST] … Touch=OK`; SELF-TEST list on screen in the same orientation as the esp32dev unit; `ui qc` → PASS/FAIL hit-boxes match finger. Crash on first draw = missing `USE_HSPI_PORT` (FAIL); mirrored touch → adjust `touch.cpp:62-63` under `#if BOARD_RAK3212` only. Rollback: revert.

### Phase 3 — RFID UART protocol discovery + parser
Entry: Phase 2 PASS. **Safety gate:** reader on 5 V (common GND), TX **not** connected; DMM the TX idle level: > 3.6 V → divider (1.8 kΩ/3.3 kΩ) or BSS138 before GPIO14; ≤ 3.3 V → direct. GPIO21 (→ reader RX) stays unwired.
Steps: `rfid raw on`; present a badge **already enrolled in the ETS backend** 3× (hold once for 3 s to measure re-emit period) + an EM4100 tag; derive LEN/TYPE/BCC; set `bcc_mode`, `RFID_UART_HOLD_GAP_MS` (≥ 2× re-emit), `RFID_UART_REVERSE_MIFARE_UID`; add captured frames as golden vectors; re-run native tests.
Gate: `[RFID] frame ok type=0x01 uid=XXXXXXXX` and `[Main] Scanned UID: XXXXXXXX` **equal to the backend's enrolled hex** (directly or after the reverse flag); one `Scanned UID` line per presentation while held; POST `RFID (UART 7941E) OK` with reader powered, `FAIL` unplugged. Any other encoding (decimal/Wiegand-derived) → stop, consult the module's output-mode config. Rollback: revert; defaults stay vendor values.

### Phase 4 — SX1262 bring-up + OTAA join + heartbeat decoded
Entry: Phase 3 PASS; **antenna connected before any TX**; bench ChirpStack v4 (`10.10.8.140`, tenant `52f14cd4-…`, AS923-1) reachable; `credentials.h` has JoinEUI/AppKey. ChirpStack: device profile "RMG RFID station" (AS923, MAC 1.0.4, RP002-1.0.3, Class A, ADR off, codec = `rfid_station_codec.js`), application, device with DevEUI from `lora show`.
Gate: `[LoRa] SX1262 up (AS923, TCXO 1.8V, DIO2 RF switch)`, POST `LoRa (SX1262) OK`; `[LoRa] JOINED (new session) DR3 SF9` within 10 s; `lora hb` → `uplink OK fPort=11 len=20`; ChirpStack Events shows join + `object.type="heartbeat"`, `fw="0.2.0"`; reboot → `session restored (no re-join)` with fcnt continuing; `lora clear-session yes` → fresh join. Signatures: `radio.begin failed (-2)` = SPI order/pins; `-1116/-6` = keys/region/gateway; `-5 TX_TIMEOUT` = DR not pinned. Rollback: revert; `lora clear-session yes` wipes NVS `lorawan`.

### Phase 5 — app integration, fallback end-to-end
Entry: Phase 4 PASS; station claimed + mapped over WiFi; operator logged in.
Script: (1) online scan → `EVENT SENT`, no LoRa frame. (2) backend stopped, WiFi up: scan → `Queued + LoRa`, `[LoRa] uplink OK fPort=10 seq=N`; ChirpStack `object.event_id="E_<epoch>_N"`, `rfid_uid` = badge hex, `operator_uid` present; heartbeat `offline_reason="SERVER_UNREACHABLE"`. (3) AP off: station stays in READY (offline mode), heartbeat `WIFI_DOWN`, scans keep uplinking. (4) restore: `[Main] Flushing N queued events` → HTTP `POST event … E_<epoch>_N` with the **same** N (de-dup confirmed with the backend team's log). (5) QC type: QC_PASS/QC_FAIL. (6) reboot mid-offline: seq continues, no duplicate seq. (7) 30 min offline soak, scan every 20 s: no drops, `sys loop` max latency < 50 ms, `lora show` stack high-water sane. Rollback: revert Phase 5 commits; 0–4 remain bench tools.

### Phase 6 — docs + esp32dev regression
README RAK3212 section (build/flash/monitor, `303A:1001`, `monitor_dtr/rts`), `docs/PIN_MAP.md` (both boards + the two no-pin-less-begin rules), `docs/LORAWAN_PAYLOAD.md` (contract, vectors, ChirpStack setup, de-dup rule), `FW_VERSION 0.2.0`.
Gate: `pio run -e esp32dev` 0 warnings, size delta vs Phase 0 ≤ 2 KB and explained; on the existing esp32dev unit boot → 5 POST lines unchanged → READY → scan → pull WiFi → `Queued (offline)` unchanged → restore → replay; `pio run -e rak3212` clean; `pio test -e native` green.

## Risks / assumptions / unknowns

| # | Item | Status | Settled by |
|---|---|---|---|
| R1 | USB is native USB-Serial-JTAG | ASSUMED | Phase 1; fallback `CDC_ON_BOOT=0` + UART0 bridge (one flag) |
| R2 | 7941E frame layout / BCC / type bytes | ASSUMED | Phase 3 raw dump; parser modes configurable |
| R3 | Reader TX at 5 V TTL (S3 not 5 V tolerant) | UNKNOWN | Phase 3 DMM gate **before** wiring GPIO14 |
| R4 | UID byte order = MFRC522 order (existing enrolments) | UNKNOWN | Phase 3 known-badge comparison; `RFID_UART_REVERSE_MIFARE_UID` |
| R5 | MSP2834 LED pin drive (80 mA) from GPIO42 | UNKNOWN | Phase 2 schematic gate; external transistor or rail-tie if it is the raw LED |
| R6 | 5 V available on J3 for reader + display; USB-only budget (~600 mA peaks) | ASSUMED | board inspection; use powered hub / bench 5 V |
| R7 | Backend accepts `E_<epoch>_<seq>` ids and can de-dup by (MAC, seq); ChirpStack→ETS integration exists | ASSUMED / out of scope (backend repo not found under `rnd-southerniot/app-rmg-rfid-ets`) | one POST on esp32dev in Phase 0; contract doc handed to backend |
| R8 | RadioLib 7.7.1 builds under Arduino core 2.0.16 | ASSUMED | Phase 0 `pio run -e rak3212` |
| R9 | 8 KB task stack, core-0 placement | ASSUMED | `lora show` high-water in Phase 4/5 |
| R10 | NVS wear: ~292 B session write per uplink on 20 KB `nvs` | fine at tens/day; UNKNOWN at high volume | Phase 5 soak; mitigation = persist every N + FCnt jump-ahead or larger `nvs` partition |
| R11 | Pre-existing: `getLocalTime()` blocks 5 s when unsynced; `ntpInit()` never re-run after reconnect | PROVEN (source) | separate one-line fix (`getLocalTime(&t, 0)`) on both boards — proposed, needs OK |
| R12 | Shared per-factory AppKey in `credentials.h` vs per-device keys | ASSUMED product choice | optional `lora creds` NVS provisioning (sibling `prov_console.cpp` pattern) |

## Verification summary
- Host: `pio test -e native` (frame parser + payload encoder golden vectors), `node tools/chirpstack/codec_test.js`.
- Build: `pio run -e esp32dev` byte-identical after Phase 0, ≤ 2 KB delta after Phase 6; `pio run -e rak3212` clean.
- Bench: Phase gates 1–5 above with exact expected serial lines; ChirpStack Events tab as the LoRa oracle; ETS backend log as the de-dup oracle.
- Out of scope (flagged, not done here): ChirpStack→ETS ingestion + de-dup in the backend repo; per-device key provisioning; downlinks (schema 1 defines none).
