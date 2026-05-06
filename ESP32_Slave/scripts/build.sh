#!/usr/bin/env bash
# build.sh — Compileaza firmware ESP32_Slave cu arduino-cli.
# Folosire: bash scripts/build.sh [--debug]
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FQBN="esp32:esp32:esp32"
PARTITION_SCHEME="min_spiffs"   # 1.9MB app + OTA partition
FLASH_SIZE="4M"

# Optional bump build number (daca exista scriptul Master)
BUMP_SCRIPT="$PROJECT_DIR/../ESP32/scripts/bump_build.sh"
if [[ -f "$BUMP_SCRIPT" ]]; then
    bash "$BUMP_SCRIPT"
fi

# Extra flags: stack 16KB + log level (INFO in debug, WARN in release)
EXTRA_FLAGS="-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384"
if [[ "${1:-}" == "--debug" ]]; then
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=1 -DCORE_DEBUG_LEVEL=2"
    echo ">>> Build DEBUG"
else
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=2"
    echo ">>> Build RELEASE"
fi

arduino-cli compile \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=$PARTITION_SCHEME,FlashSize=$FLASH_SIZE" \
    --warnings default \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    "$PROJECT_DIR"

echo "✅ Build OK: $PROJECT_DIR"
