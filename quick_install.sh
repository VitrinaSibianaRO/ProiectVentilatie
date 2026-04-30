#!/bin/bash
# ==============================================================================
# Quick install: build Release local + deploy pe Waydroid
# ==============================================================================
set -e

PROJECT_DIR="/mnt/81d17934-0657-41a6-8caf-5075bc128310/dev/ProiectVentilatie"
PACKAGE_NAME="com.proiect.ventilatie"

cd "$PROJECT_DIR"

# 1. Verificari
command -v waydroid >/dev/null 2>&1 || { echo "❌ waydroid nu este instalat."; exit 1; }
command -v dotnet   >/dev/null 2>&1 || { echo "❌ dotnet nu este instalat."; exit 1; }

# 2. Pornire Waydroid daca nu ruleaza
if ! waydroid status 2>/dev/null | grep -qE "Session:[[:space:]]*RUNNING"; then
    echo "🟢 Pornesc sesiunea Waydroid..."
    waydroid prop set persist.waydroid.gles 1 >/dev/null 2>&1 || true
    waydroid prop set persist.waydroid.suspend false >/dev/null 2>&1 || true
    waydroid session start >/dev/null 2>&1 &

    echo -n "   Astept initializarea"
    for i in {1..40}; do
        if timeout 2 waydroid app list 2>/dev/null | grep -q "com.android"; then
            break
        fi
        echo -n "."
        sleep 2
    done
    echo ""

    if ! timeout 5 waydroid app list 2>/dev/null | grep -q "com.android"; then
        echo "❌ Nu am putut porni Waydroid. Incearca: sudo waydroid container restart"
        exit 1
    fi
fi

# Dezghetare daca e cazul
if waydroid status 2>/dev/null | grep -q "FROZEN"; then
    echo "🔓 Container inghetat, dezghet..."
    sudo waydroid container unfreeze >/dev/null 2>&1 || true
fi

# 3. Detecteaza ABI Waydroid (de obicei x86_64)
WAYDROID_ABI=$(waydroid shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r\n')
case "$WAYDROID_ABI" in
    arm64-v8a)   RID="android-arm64" ;;
    armeabi-v7a) RID="android-arm"   ;;
    x86_64)      RID="android-x64"   ;;
    x86)         RID="android-x86"   ;;
    *)           RID="android-x64"   ;;  # fallback (Waydroid default = x86_64)
esac
echo "📐 Waydroid ABI: ${WAYDROID_ABI:-necunoscut} → RID: $RID"

# 4. Build Release
echo "🔨 Build Release ($RID)..."
dotnet publish MobileApp \
    -f net10.0-android \
    -c Release \
    -r "$RID" \
    -p:AndroidPackageFormats=apk \
    --nologo --verbosity minimal

# 5. Gaseste APK-ul cel mai recent
APK_PATH=$(find MobileApp/bin/Release -name "*-Signed.apk" -printf "%T@ %p\n" 2>/dev/null \
    | sort -nr | head -1 | cut -d' ' -f2-)

if [ -z "$APK_PATH" ] || [ ! -f "$APK_PATH" ]; then
    echo "❌ Nu am gasit APK-ul construit."
    find MobileApp/bin/Release -name "*.apk" 2>/dev/null
    exit 1
fi
echo "📦 APK: $APK_PATH"

# 6. Porneste UI Waydroid (daca nu e deja deschis)
waydroid show-full-ui >/dev/null 2>&1 &
sleep 1

# 7. Dezinstalare versiune veche + instalare proaspata
echo "🗑️  Dezinstalez versiunea veche..."
waydroid shell pm uninstall "$PACKAGE_NAME" >/dev/null 2>&1 || true

echo "📲 Instalez APK pe Waydroid..."
waydroid app install "$APK_PATH"

# 8. Lansare aplicatie
echo "🚀 Lansez aplicatia..."
waydroid shell am force-stop "$PACKAGE_NAME" >/dev/null 2>&1 || true
waydroid app launch "$PACKAGE_NAME"

echo "✅ Gata! $PACKAGE_NAME ruleaza pe Waydroid."
