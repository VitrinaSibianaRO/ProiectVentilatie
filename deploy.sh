#!/bin/bash

# ==============================================================================
# SCRIPT DEPLOY AUTOMAT: GitHub Actions -> Waydroid / Emulator / Telefon fizic
# ==============================================================================
# Functionalitate:
# 1. Descarca ultimul APK valid de pe GitHub
# 2. Auto-detecteaza target-ul (waydroid sau adb device) sau il alege user-ul
# 3. Instaleaza aplicatia
# 4. Lanseaza aplicatia automat
#
# Utilizare:
#   ./deploy.sh                  # auto: detecteaza waydroid sau adb device
#   ./deploy.sh waydroid         # forteaza Waydroid
#   ./deploy.sh adb              # forteaza adb (telefon fizic / emulator)
#   ./deploy.sh both             # instaleaza pe AMBELE (waydroid + adb)
# ==============================================================================

set -e

# --- CONFIGURARE PROIECT ---
REPO="RaduOvidiu20/ProiectVentilatie"
ARTIFACT_NAME="Android-APK"
WORKFLOW_NAME="Android CI/CD"
PACKAGE_NAME="com.proiect.ventilatie"
DOWNLOAD_DIR="$(pwd)/APK"
EMULATOR_NAME="Pixel_5_API_34"

# --- ARGUMENT TARGET ---
TARGET="${1:-auto}"   # auto | waydroid | adb | both

echo "------------------------------------------------"
echo "🚀 Pornire proces Deploy pentru $PACKAGE_NAME"
echo "   Target: $TARGET"
echo "------------------------------------------------"

# --- HELPERS ---
have_waydroid() { command -v waydroid >/dev/null 2>&1; }
have_adb()      { command -v adb >/dev/null 2>&1; }

waydroid_running() {
    have_waydroid || return 1
    timeout 3 waydroid status 2>/dev/null | grep -qE "Session:[[:space:]]*RUNNING"
}

adb_device_present() {
    have_adb || return 1
    adb devices 2>/dev/null | grep -v "List" | grep -q "device$"
}

# 1. Verificare dependente generale
have_adb || { echo "❌ Eroare: adb nu este instalat."; exit 1; }
command -v gh >/dev/null 2>&1 || { echo "❌ Eroare: GitHub CLI (gh) nu este instalat."; exit 1; }

# 2. Verificare autentificare GitHub
gh auth status >/dev/null 2>&1 || {
    echo "⚠️ Eroare: Trebuie sa fii logat in gh. Ruleaza: gh auth login"
    exit 1
}

# 3. Descarcare APK
echo "⬇️ Pas 1: Descarcare ultimul APK de pe GitHub..."
mkdir -p "$DOWNLOAD_DIR"
rm -f "$DOWNLOAD_DIR"/*.apk

RUN_ID=$(gh run list --repo "$REPO" --workflow "$WORKFLOW_NAME" --status success --limit 1 --json databaseId --jq '.[0].databaseId')

if [ -z "$RUN_ID" ]; then
    echo "❌ Eroare: Nu am gasit niciun build reusit pe GitHub."
    exit 1
fi

echo "   > ID Build: $RUN_ID"
gh run download "$RUN_ID" --repo "$REPO" --name "$ARTIFACT_NAME" --dir "$DOWNLOAD_DIR"

# 4. Identificare APK
APK_PATH=$(find "$DOWNLOAD_DIR" -name "*-Signed.apk" | head -n 1)
[ -z "$APK_PATH" ] && APK_PATH=$(find "$DOWNLOAD_DIR" -name "*.apk" | head -n 1)
if [ -z "$APK_PATH" ]; then
    echo "❌ Eroare: Nu am gasit niciun fisier .apk in $DOWNLOAD_DIR"
    exit 1
fi
echo "   > APK gasit: $(basename "$APK_PATH")"

# 5. Auto-detect target
if [ "$TARGET" = "auto" ]; then
    if waydroid_running; then
        echo "   > Detectat: Waydroid sesiune activa"
        TARGET="waydroid"
    elif adb_device_present; then
        echo "   > Detectat: device adb conectat"
        TARGET="adb"
    elif have_waydroid; then
        echo "   > Niciun device activ. Pornesc Waydroid..."
        TARGET="waydroid"
    else
        echo "   > Niciun device. Pornesc emulator standard..."
        TARGET="adb"
    fi
fi

# ============================================================================
# FUNCTII INSTALARE / LANSARE
# ============================================================================

deploy_waydroid() {
    have_waydroid || { echo "❌ Waydroid nu este instalat."; return 1; }

    echo ""
    echo "🟢 === WAYDROID ==="

    if ! waydroid_running; then
        echo "   > Configurare dimensiuni Mobile (450x900)..."
        waydroid prop set persist.waydroid.width 450  >/dev/null 2>&1 || true
        waydroid prop set persist.waydroid.height 900 >/dev/null 2>&1 || true
        waydroid prop set persist.waydroid.dpi 280    >/dev/null 2>&1 || true
        waydroid prop set persist.waydroid.gles 1     >/dev/null 2>&1 || true
        waydroid prop set persist.waydroid.suspend false >/dev/null 2>&1 || true

        echo "   > Resetare sesiune Waydroid..."
        waydroid session stop >/dev/null 2>&1 || true
        pkill -9 waydroid >/dev/null 2>&1 || true
        pkill -9 weston   >/dev/null 2>&1 || true
        sleep 2

        echo "   > Pornire sesiune Waydroid..."
        waydroid session start >/dev/null 2>&1 &

        echo "   > Astept initializarea..."
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
            return 1
        fi
        echo "   > Waydroid este GATA."
    else
        echo "   > Waydroid deja pornit."
    fi

    # Dezghetare daca e cazul
    if waydroid status 2>/dev/null | grep -q "FROZEN"; then
        echo "   > Container inghetat. Dezghet..."
        sudo waydroid container unfreeze >/dev/null 2>&1 || true
    fi

    waydroid show-full-ui >/dev/null 2>&1 &
    sleep 1

    echo "   > Dezinstalare versiune veche..."
    waydroid shell pm uninstall "$PACKAGE_NAME" >/dev/null 2>&1 || true

    echo "   > Instalare APK..."
    waydroid app install "$APK_PATH"

    echo "   > Lansare aplicatie..."
    waydroid shell am force-stop "$PACKAGE_NAME" >/dev/null 2>&1 || true
    waydroid app launch "$PACKAGE_NAME"

    echo "   ✅ Waydroid: deploy reusit."
}

deploy_adb() {
    have_adb || { echo "❌ adb nu este instalat."; return 1; }

    echo ""
    echo "🟢 === ADB (telefon fizic / emulator) ==="

    adb start-server >/dev/null

    DEVICE=$(adb devices | grep -v "List" | grep "device$" | awk '{print $1}' | head -n 1)

    if [ -z "$DEVICE" ]; then
        echo "   > Niciun device adb. Pornesc emulator $EMULATOR_NAME..."

        EMULATOR_BIN="$HOME/Android/Sdk/emulator/emulator"
        [ ! -f "$EMULATOR_BIN" ] && EMULATOR_BIN=$(command -v emulator || echo "")

        if [ -z "$EMULATOR_BIN" ]; then
            echo "❌ Nu am gasit executabilul 'emulator'."
            return 1
        fi

        "$EMULATOR_BIN" -avd "$EMULATOR_NAME" -accel on -gpu host >/dev/null 2>&1 &

        echo "   > Astept boot-ul..."
        adb wait-for-device
        while [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]; do
            sleep 2
        done
        DEVICE=$(adb devices | grep -v "List" | grep "device$" | awk '{print $1}' | head -n 1)
        echo "   > Emulator GATA: $DEVICE"
    else
        echo "   > Device detectat: $DEVICE"
    fi

    echo "   > Dezinstalare versiune veche..."
    adb -s "$DEVICE" uninstall "$PACKAGE_NAME" >/dev/null 2>&1 || true

    echo "   > Instalare APK..."
    adb -s "$DEVICE" install -r "$APK_PATH"

    echo "   > Lansare aplicatie..."
    adb -s "$DEVICE" shell monkey -p "$PACKAGE_NAME" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1

    echo "   ✅ adb ($DEVICE): deploy reusit."
}

# ============================================================================
# EXECUTIE
# ============================================================================

case "$TARGET" in
    waydroid)
        deploy_waydroid
        ;;
    adb)
        deploy_adb
        ;;
    both)
        deploy_waydroid || echo "⚠️ Waydroid a esuat, continui cu adb..."
        deploy_adb      || echo "⚠️ adb a esuat."
        ;;
    *)
        echo "❌ Target necunoscut: $TARGET (foloseste: auto | waydroid | adb | both)"
        exit 1
        ;;
esac

echo ""
echo "------------------------------------------------"
echo "✅ DEPLOY FINALIZAT"
echo "------------------------------------------------"
