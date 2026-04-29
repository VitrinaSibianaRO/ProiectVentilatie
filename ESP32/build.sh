#!/bin/bash
# ESP32 firmware build wrapper.
# 1. Increments build number (regenerates Version.h)
# 2. Compiles firmware via arduino-cli
# 3. Optionally uploads to ESP32 if PORT is provided as $1
#
# Usage:
#   bash ESP32/build.sh                  # compile only
#   bash ESP32/build.sh /dev/ttyUSB0     # compile + upload

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-}"
FQBN="esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M,PartitionScheme=default,CPUFreq=240"

echo "------------------------------------------------"
echo "ESP32 firmware build"
echo "------------------------------------------------"

# 1. Bump build number
bash "$DIR/scripts/bump_build.sh"

# 2. Verify arduino-cli available
command -v arduino-cli >/dev/null 2>&1 || {
    echo "Error: arduino-cli not installed. See docs/03-esp32-build.md"
    exit 1
}

# 3. Compile (and upload if port provided)
if [ -n "$PORT" ]; then
    echo "Compiling and uploading to $PORT ..."
    arduino-cli compile --upload --port "$PORT" --fqbn "$FQBN" "$DIR"
    echo "Upload complete."
else
    echo "Compiling (no upload) ..."
    arduino-cli compile --fqbn "$FQBN" --output-dir "$DIR/build" "$DIR"
    echo "Compile complete. Binary: $DIR/build/"
fi

echo "------------------------------------------------"
echo "Done. Build #$(cat $DIR/build_number.txt)"
echo "------------------------------------------------"
