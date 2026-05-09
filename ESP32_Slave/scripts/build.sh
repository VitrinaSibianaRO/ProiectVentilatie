#!/usr/bin/env bash
# build.sh — Compileaza firmware ESP32_Slave (Carbon S2, ESP32-S2FN4R2).
# Folosire:
#   bash scripts/build.sh           → release
#   bash scripts/build.sh --debug   → debug verbose
#
# CDCOnBoot=cdc: USB Serial Monitor activ. UART RX pe GPIO3 (nu GPIO18/19=USB).
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$PROJECT_DIR/build"

# ESP32-S2 (GroundStudio Carbon S2)
FQBN="esp32:esp32:esp32s2"

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
    --board-options "PartitionScheme=custom,PSRAM=enabled,CDCOnBoot=cdc" \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    --build-property "build.partitions=$PROJECT_DIR/partitions.csv" \
    --output-dir "$OUTPUT_DIR" \
    --warnings default \
    "$PROJECT_DIR"

echo "✅ Build OK → $OUTPUT_DIR"
