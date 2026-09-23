#!/usr/bin/env bash
# scripts/flash.sh — canonical build + flash wrapper.
#
#   scripts/flash.sh esp32dev [PORT]          # PlatformIO upload (CP2102N bridge)
#   scripts/flash.sh rak3212  [PORT]          # build with pio, flash with esptool directly
#   scripts/flash.sh rak3212-bringup [PORT]
#
# Why esptool directly for the RAK3212: PlatformIO's `-t upload` runs esptool with the default
# connect attempts, and on this board's native USB-Serial-JTAG it failed twice with
# "No serial data received" while the application was running (2026-09-24). The espressif32
# builder does not pass `upload_flags` to esptool, so the retry count cannot be set in
# platformio.ini. esptool with --connect-attempts 30 flashed cleanly both times.
# WRITE only — never erases the flash (NVS holds tokens, mapping, queue, LoRa session).
set -euo pipefail

ENV="${1:-}"; PORT="${2:-}"
[[ -n "$ENV" ]] || { echo "usage: $0 <esp32dev|rak3212|rak3212-bringup> [port]" >&2; exit 2; }

cd "$(dirname "$0")/.."
pio run -e "$ENV"

if [[ "$ENV" == "esp32dev" ]]; then
    exec pio run -e "$ENV" -t upload ${PORT:+--upload-port "$PORT"}
fi

# RAK3212 (ESP32-S3, 16 MB, DIO 80 MHz, offsets of the espressif32 Arduino layout)
if [[ -z "$PORT" ]]; then
    PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    [[ -n "$PORT" ]] || { echo "no /dev/cu.usbmodem* port found (RAK3212 unplugged?)" >&2; exit 3; }
fi
BUILD=".pio/build/$ENV"
ESPTOOL="$(ls -d "$HOME"/.platformio/packages/tool-esptoolpy*/esptool.py | head -1)"
FRAMEWORK="$(ls -d "$HOME"/.platformio/packages/framework-arduinoespressif32@3.20016* 2>/dev/null | head -1 || true)"
[[ -n "$FRAMEWORK" ]] || FRAMEWORK="$HOME/.platformio/packages/framework-arduinoespressif32"
BOOT_APP0="$FRAMEWORK/tools/partitions/boot_app0.bin"
[[ -f "$BOOT_APP0" ]] || { echo "boot_app0.bin not found under $FRAMEWORK" >&2; exit 4; }
PY="$(ls "$HOME"/.platformio/penv/bin/python3 2>/dev/null || command -v python3)"

echo "flashing $ENV → $PORT (esptool, 30 connect attempts, write only)"
exec "$PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 \
    --before default_reset --after hard_reset --connect-attempts 30 \
    write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0     "$BUILD/bootloader.bin" \
    0x8000  "$BUILD/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"
