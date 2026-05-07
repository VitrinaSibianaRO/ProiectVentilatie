#!/usr/bin/env bash
# flash.sh — Flash firmware ESP32_Slave (Carbon V3, ESP32-PICO-V3-02).
# Folosire: bash scripts/flash.sh [/dev/ttyUSB1]
# Default port: /dev/ttyUSB1 (Slave). Master e de obicei /dev/ttyUSB0.
#
# Ruleaza intai build.sh — flash.sh foloseste binarele din ESP32_Slave/build/.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$PROJECT_DIR/build"
FQBN="esp32:esp32:esp32"
PORT="${1:-/dev/ttyUSB1}"

if [[ ! -e "$PORT" ]]; then
    echo "❌ Port $PORT nu exista. Placi conectate:"
    ls /dev/ttyUSB* 2>/dev/null || echo "   (niciunul detectat)"
    exit 1
fi

if [[ ! -d "$OUTPUT_DIR" ]] || [[ -z "$(ls "$OUTPUT_DIR"/*.bin 2>/dev/null)" ]]; then
    echo "⚠️  Nu exista build compilat. Ruleaza mai intai:"
    echo "   bash scripts/build.sh"
    exit 1
fi

echo "📤 Flash Slave → $PORT"
arduino-cli upload \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=custom" \
    --port "$PORT" \
    --verify \
    --input-dir "$OUTPUT_DIR"

echo "✅ Flashed Slave OK → $PORT"
