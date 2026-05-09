#!/bin/bash

# ==============================================================================
# SCRIPT BUILD AUTOMAT APK - .NET MAUI
# ==============================================================================
# Functionalitate:
# 1. Curata build-urile anterioare
# 2. Incrementeaza numarul de build (via Target in .csproj)
# 3. Compileaza si semneaza APK-ul (Release)
# 4. Copiaza rezultatul in folderul /APK din radacina
# ==============================================================================

set -e

# --- CONFIGURARE ---
DOTNET_EXE="/home/ovidiu/.dotnet/dotnet"
if [ ! -f "$DOTNET_EXE" ]; then
    DOTNET_EXE=$(command -v dotnet || echo "")
fi

if [ -z "$DOTNET_EXE" ]; then
    echo "❌ Eroare: Nu am gasit executabilul dotnet. Verifica instalarea."
    exit 1
fi

PROJECT_NAME="ProiectVentilatie.Mobile"
MOBILE_DIR="MobileApp"
ROOT_DIR="$(pwd)"
OUTPUT_DIR="$ROOT_DIR/APK"
TIMESTAMP=$(date +"%Y%m%d_%H%M")

echo "------------------------------------------------"
echo "🚀 Incepere proces Build APK: $PROJECT_NAME"
echo "   Ora: $(date)"
echo "------------------------------------------------"

# 1. Intrare in folderul proiectului
cd "$MOBILE_DIR" || { echo "❌ Nu am putut accesa folderul $MOBILE_DIR"; exit 1; }

# 2. Curatare
echo "🧹 Pas 1: Curatare build-uri vechi..."
"$DOTNET_EXE" clean -c Release -v q

# 3. Build si Publish
echo "🏗️  Pas 2: Compilare si semnare APK (net10.0-android)..."
echo "   (Acest proces poate dura cateva minute din cauza optimizarilor AOT)"

"$DOTNET_EXE" publish -f net10.0-android -c Release \
    -p:AndroidPackageFormat=apk \
    -p:AndroidKeyStore=true \
    -p:AndroidSigningKeyStore=ventilatie.keystore \
    -p:AndroidSigningKeyAlias=ventilatie \
    -p:AndroidSigningKeyPass=ventilatie123 \
    -p:AndroidSigningStorePass=ventilatie123 \
    --no-self-contained

# 4. Verificare si mutare rezultat
APK_SOURCE="bin/Release/net10.0-android/publish/com.proiect.ventilatie-Signed.apk"

if [ -f "$APK_SOURCE" ]; then
    mkdir -p "$OUTPUT_DIR"
    
    # Copiem ca "latest" pentru deploy-ul automat
    cp -f "$APK_SOURCE" "$OUTPUT_DIR/ventilatie-latest.apk"
    
    # Facem si o copie cu timestamp pentru istoric
    cp -f "$APK_SOURCE" "$OUTPUT_DIR/ventilatie_$TIMESTAMP.apk"
    
    echo "------------------------------------------------"
    echo "✅ BUILD REUSIT!"
    echo "📦 APK principal: $OUTPUT_DIR/ventilatie-latest.apk"
    echo "📦 Backup: $OUTPUT_DIR/ventilatie_$TIMESTAMP.apk"
    echo "------------------------------------------------"
else
    echo "❌ Eroare: Build-ul a raportat succes, dar APK-ul nu a fost gasit la:"
    echo "   $APK_SOURCE"
    exit 1
fi
