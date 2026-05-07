#!/usr/bin/env bash
# build.sh — Compileaza firmware ESP32_Slave (Carbon V3, ESP32-PICO-V3-02).
# Folosire: bash scripts/build.sh [--debug]
#
# Directorul se numeste deja ESP32_Slave la fel ca ESP32_Slave.ino,
# deci arduino-cli poate compila direct fara workaround.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$PROJECT_DIR/build"

FQBN="esp32:esp32:esp32"

# Optional bump build number (daca exista scriptul Master)
BUMP_SCRIPT="$PROJECT_DIR/../ESP32/scripts/bump_build.sh"
if [[ -f "$BUMP_SCRIPT" ]]; then
    bash "$BUMP_SCRIPT"
fi

EXTRA_FLAGS="-DBOARD_HAS_PSRAM -DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384"

if [[ "${1:-}" == "--debug" ]]; then
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=1 -DCORE_DEBUG_LEVEL=2"
    echo ">>> Build DEBUG (Slave)"
else
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=2"
    echo ">>> Build RELEASE (Slave)"
fi

mkdir -p "$OUTPUT_DIR"

arduino-cli compile \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=custom,PSRAM=enabled" \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    --build-property "build.partitions=$PROJECT_DIR/partitions.csv" \
    --output-dir "$OUTPUT_DIR" \
    --warnings default \
    "$PROJECT_DIR"

echo "✅ Build OK → $OUTPUT_DIR"
