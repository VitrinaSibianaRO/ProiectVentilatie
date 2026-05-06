#!/usr/bin/env bash
# flash.sh — Flash firmware ESP32_Slave pe placa via arduino-cli.
# Folosire: bash scripts/flash.sh [/dev/ttyUSB1]
# Default port: /dev/ttyUSB1 (Slave). Master e de obicei /dev/ttyUSB0.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FQBN="esp32:esp32:esp32"
PARTITION_SCHEME="min_spiffs"
FLASH_SIZE="4M"
PORT="${1:-/dev/ttyUSB1}"

if [[ ! -e "$PORT" ]]; then
    echo "❌ Port $PORT nu exista. Placi conectate:"
    ls /dev/ttyUSB* 2>/dev/null || echo "   (niciunul detectat)"
    exit 1
fi

echo "📤 Flash → $PORT"
arduino-cli upload \
    --fqbn "$FQBN" \
    --board-options "PartitionScheme=$PARTITION_SCHEME,FlashSize=$FLASH_SIZE" \
    --port "$PORT" \
    --verify \
    "$PROJECT_DIR"

echo "✅ Flashed OK → $PORT"
