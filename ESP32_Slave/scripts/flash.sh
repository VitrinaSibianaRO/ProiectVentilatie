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

if command -v esptool.py >/dev/null 2>&1; then
    echo "🧹 Erase coredump partition region (0x3F0000..0x400000)"
    esptool.py --chip esp32 --port "$PORT" erase_region 0x3F0000 0x10000
elif command -v esptool >/dev/null 2>&1; then
    echo "🧹 Erase coredump partition region (0x3F0000..0x400000)"
    esptool --chip esp32 --port "$PORT" erase_region 0x3F0000 0x10000
else
    echo "⚠️  esptool nu este in PATH; daca apare 'core dump config corrupted', ruleaza o stergere completa a flash-ului o singura data."
fi

arduino-cli upload \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=custom" \
    --port "$PORT" \
    --verify \
    --input-dir "$OUTPUT_DIR"

echo "✅ Flashed Slave OK → $PORT"
