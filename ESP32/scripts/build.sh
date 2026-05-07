#!/usr/bin/env bash
# build.sh — Compileaza firmware ESP32 Master (Carbon V3, ESP32-PICO-V3-02).
# Folosire: bash scripts/build.sh [--debug]
#
# Nota: arduino-cli cere ca directorul sa se numeasca la fel cu .ino-ul.
# Directorul real se numeste ESP32/ dar .ino-ul e ProiectVentilatie.ino,
# asa ca sincronizam sursele in /tmp/ProiectVentilatie/ inainte de compilare.
set -euo pipefail

REAL_DIR="$(cd "$(dirname "$0")/.." && pwd)"   # calea reala: .../ESP32
BUILD_SKETCH="/tmp/ProiectVentilatie"           # tmp cu numele corect
OUTPUT_DIR="$REAL_DIR/build"                   # binare .bin/.elf salvate aici

FQBN="esp32:esp32:esp32"

# Bump build number (scrie Version.h in REAL_DIR)
bash "$REAL_DIR/scripts/bump_build.sh"

# Sincronizeaza sursele in /tmp — exclude directoare care nu sunt sursa
rsync -a --delete \
    --exclude 'build/' \
    --exclude 'scripts/' \
    --exclude '.git/' \
    --exclude 'docs/' \
    "$REAL_DIR/" "$BUILD_SKETCH/"

EXTRA_FLAGS="-DBOARD_HAS_PSRAM -DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384"

if [[ "${1:-}" == "--debug" ]]; then
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=1 -DCORE_DEBUG_LEVEL=2"
    echo ">>> Build DEBUG (Master)"
else
    EXTRA_FLAGS="$EXTRA_FLAGS -DLOG_LEVEL=2"
    echo ">>> Build RELEASE (Master)"
fi

mkdir -p "$OUTPUT_DIR"

arduino-cli compile \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=custom,PSRAM=enabled" \
    --build-property "build.extra_flags=$EXTRA_FLAGS" \
    --build-property "build.partitions=$REAL_DIR/partitions.csv" \
    --output-dir "$OUTPUT_DIR" \
    --warnings default \
    "$BUILD_SKETCH"

echo "✅ Build OK → $OUTPUT_DIR"
