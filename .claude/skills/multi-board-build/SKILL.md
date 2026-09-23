---
name: multi-board-build
description: How this repo builds two boards from one codebase — the esp32dev/rak3212/native PlatformIO envs, per-board pin headers under src/boards, backend selection by build_src_filter, the inline-stub pattern that keeps main.cpp free of #ifdefs, the esp32dev regression gate (sizes identical after restructures, ≤ 2 KB after features), and how to add a third board. Use when changing platformio.ini, pins, or adding board-specific modules.
---

# Multi-board build

## Layout

- `platformio.ini`: `[env]` shared libs, `[tft_fonts]`, `esp32dev` (values unchanged from the
  single-env original), `rak3212` (esp32-s3-devkitc-1, `qio_opi`, 16 MB, `default_16MB.csv`,
  USB CDC on boot, TFT_eSPI `USE_HSPI_PORT=1`, RadioLib pinned 7.7.1), `rak3212-bringup`, `native`
  (Unity; must override `lib_deps` or PlatformIO compiles TFT_eSPI on the host).
- `src/config.h` selects `src/boards/board_<env>.h` via `-DBOARD_ESP32DEV` / `-DBOARD_RAK3212`
  and keeps board-independent timings. TFT pins live only in `platformio.ini`.
- Backends by `build_src_filter`: `rfid_mfrc522.cpp` vs `rfid_uart.cpp`+`rfid_frame.cpp`;
  `lora_link.cpp`, `lora_payload.cpp`, `serial_console.cpp` only on `rak3212`.
- Board capability flags: `BOARD_HAS_LORA`, `BOARD_HAS_CONSOLE`, `LCD_POST_READ_ID`,
  `LED_BACKEND_RGB` / `LED_BACKEND_NEOPIXEL`, `RFID_BACKEND_*`. Headers `lora_link.h` and
  `serial_console.h` provide inline no-op stubs when the flag is 0 so `main.cpp` has no `#ifdef`s.

## Regression gate for the untouched board

After a restructure: `pio run -e esp32dev` RAM/Flash **byte-identical** (hashes differ with link
order; compare sizes). After a feature: delta ≤ 2 KB, 0 `src/` warnings, and the POST strings
unchanged (use `RFID_READER_NAME` / `LED_NAME` so the esp32dev strings stay literal-identical).
Baseline: RAM 52120 B / Flash 1082433 B at `pre-rak3212`.

## Adding a board

1. `src/boards/board_<name>.h` with pins, capability flags, backend selection.
2. `[env:<name>]` with `-DBOARD_<NAME>=1`, TFT flags, `build_src_filter` excluding the other
   backends, `lib_deps` for its radio/reader libraries.
3. Row in `docs/PIN_MAP.md` and the summary table in `CLAUDE.md`.
4. Rules to check for any ESP32-S3 board: no pin-less `Wire.begin()`/`SPI.begin()`, TFT_eSPI on
   HSPI, LEDC channel/timer sharing.
