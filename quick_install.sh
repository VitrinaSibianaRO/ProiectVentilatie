#!/bin/bash

# Configurare
APK_DIR="/mnt/81d17934-0657-41a6-8caf-5075bc128310/dev/ProiectVentilatie/APK"
PACKAGE_NAME="com.proiect.ventilatie"

echo "🔍 Caut ultimul APK semnat..."
# Gaseste cel mai nou fisier care contine '-Signed.apk'
APK_PATH=$(ls -t "$APK_DIR"/*-Signed.apk 2>/dev/null | head -n 1)

if [ -z "$APK_PATH" ]; then
    echo "❌ Eroare: Nu am gasit niciun APK semnat in $APK_DIR"
    exit 1
fi

echo "📦 Instalez: $(basename "$APK_PATH")"
waydroid app install "$APK_PATH"

echo "🚀 Lansez aplicatia: $PACKAGE_NAME"
waydroid app launch "$PACKAGE_NAME"

echo "✅ Gata!"
