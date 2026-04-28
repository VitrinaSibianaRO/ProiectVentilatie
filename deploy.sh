#!/bin/bash

# ==============================================================================
# SCRIPT DEPLOY AUTOMAT: GitHub Actions -> Emulator/Dispozitiv
# ==============================================================================
# Functionalitate:
# 1. Descarca ultimul APK valid de pe GitHub
# 2. Porneste emulatorul (daca nu este pornit deja)
# 3. Instaleaza aplicatia
# 4. Lanseaza aplicatia automat
# ==============================================================================

set -e

# --- CONFIGURARE PROIECT ---
REPO="RaduOvidiu20/ProiectVentilatie"
ARTIFACT_NAME="Android-APK"
WORKFLOW_NAME="Android CI/CD"
PACKAGE_NAME="com.proiect.ventilatie"
DOWNLOAD_DIR="$(pwd)/APK"

# --- CONFIGURARE DISPOZITIV ---
# Seteaza USE_WAYDROID=true pentru a folosi Waydroid in loc de emulatorul standard
USE_WAYDROID=true
EMULATOR_NAME="Pixel_5_API_34" 

echo "------------------------------------------------"
echo "🚀 Pornire proces Deploy pentru $PACKAGE_NAME"
echo "------------------------------------------------"

# 1. Verificare dependente
command -v adb >/dev/null 2>&1 || { echo "❌ Eroare: adb nu este instalat."; exit 1; }
command -v gh >/dev/null 2>&1 || { echo "❌ Eroare: GitHub CLI (gh) nu este instalat."; exit 1; }

# 2. Verificare autentificare GitHub
gh auth status >/dev/null 2>&1 || {
    echo "⚠️ Eroare: Trebuie sa fii logat in gh. Ruleaza: gh auth login"
    exit 1
}

# 3. Descarcare APK
echo "⬇️ Pas 1: Descarcare ultimul APK de pe GitHub..."
mkdir -p "$DOWNLOAD_DIR"
# Curatam APK-urile vechi pentru a nu exista confuzii
rm -f "$DOWNLOAD_DIR"/*.apk

RUN_ID=$(gh run list --repo "$REPO" --workflow "$WORKFLOW_NAME" --status success --limit 1 --json databaseId --jq '.[0].databaseId')

if [ -z "$RUN_ID" ]; then
    echo "❌ Eroare: Nu am gasit niciun build reusit pe GitHub."
    exit 1
fi

echo "   > ID Build: $RUN_ID"
gh run download "$RUN_ID" --repo "$REPO" --name "$ARTIFACT_NAME" --dir "$DOWNLOAD_DIR"

# 4. Identificare fisier APK
APK_PATH=$(find "$DOWNLOAD_DIR" -name "*-Signed.apk" | head -n 1)
if [ -z "$APK_PATH" ]; then
    APK_PATH=$(find "$DOWNLOAD_DIR" -name "*.apk" | head -n 1)
fi

if [ -z "$APK_PATH" ]; then
    echo "❌ Eroare: Nu am gasit niciun fisier .apk in $DOWNLOAD_DIR"
    exit 1
fi
echo "   > APK gasit: $(basename "$APK_PATH")"

# 5. Gestionare Dispozitiv / Emulator
echo "📱 Pas 2: Verificare conexiune Android..."

if [ "$USE_WAYDROID" = true ]; then
    # 0. Verificare existenta comanda waydroid
    if ! command -v waydroid >/dev/null 2>&1; then
        echo "❌ Eroare: Comanda 'waydroid' nu a fost gasita."
        exit 1
    fi

    echo "   > Optimizare Desktop (Centrare ferestre)..."
    gsettings set org.gnome.mutter center-new-windows true > /dev/null 2>&1 || true

    echo "   > Configurare dimensiuni Mobile (450x900)..."
    # Setam proprietatile pentru aspect ratio de telefon (Nativ)
    waydroid prop set persist.waydroid.width 450 > /dev/null 2>&1 || true
    waydroid prop set persist.waydroid.height 900 > /dev/null 2>&1 || true
    waydroid prop set persist.waydroid.dpi 280 > /dev/null 2>&1 || true
    # Fix pentru stabilitate grafica si prevenire crash/freeze
    waydroid prop set persist.waydroid.gles 1 > /dev/null 2>&1 || true
    waydroid prop set persist.waydroid.suspend false > /dev/null 2>&1 || true

    echo "   > Resetare sesiune Waydroid..."
    waydroid session stop > /dev/null 2>&1 || true
    pkill -9 waydroid > /dev/null 2>&1 || true
    pkill -9 weston > /dev/null 2>&1 || true # Curatam si weston daca a ramas
    sleep 2

    echo "   > Pornire sesiune Waydroid Nativ..."
    # Pornim sesiunea direct pe display-ul tau actual (Wayland)
    waydroid session start > /dev/null 2>&1 &
    
    echo "   > Se asteapta initializarea..."
    for i in {1..40}; do
        if timeout 2 waydroid app list 2>/dev/null | grep -q "com.android"; then 
            break
        fi
        echo -n "."
        sleep 2
    done
    echo ""
    
    # Verificare finala
    if timeout 5 waydroid app list 2>/dev/null | grep -q "com.android"; then
        echo "   > Waydroid este GATA."
        # Deschidem interfata Full UI daca nu e deschisa deja
        waydroid show-full-ui > /dev/null 2>&1 &
        DEVICE="waydroid"
    else
        echo "❌ Eroare: Nu am putut porni Waydroid. Incearca 'sudo waydroid container restart' manual."
        exit 1
    fi
else
    adb start-server >/dev/null
    # Verificam daca exista deja un device conectat
    DEVICE=$(adb devices | grep -v "List" | grep "device$" | awk '{print $1}' | head -n 1)

    if [ -z "$DEVICE" ]; then
        echo "   > Niciun dispozitiv detectat. Pornire emulator ($EMULATOR_NAME)..."
        
        # Cautare cale emulator
        EMULATOR_BIN="$HOME/Android/Sdk/emulator/emulator"
        if [ ! -f "$EMULATOR_BIN" ]; then
            EMULATOR_BIN=$(command -v emulator || echo "")
        fi

        if [ -z "$EMULATOR_BIN" ]; then
            echo "❌ Eroare: Nu am gasit executabilul 'emulator'. Verifica PATH-ul."
            exit 1
        fi

        "$EMULATOR_BIN" -avd "$EMULATOR_NAME" -accel on -gpu host >/dev/null 2>&1 &
        
        echo "   > Se asteapta boot-ul..."
        adb wait-for-device
        while [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]; do
            sleep 2
        done
        echo "   > Emulatorul este GATA."
        DEVICE=$(adb devices | grep -v "List" | grep "device$" | awk '{print $1}' | head -n 1)
    else
        echo "   > Dispozitiv detectat: $DEVICE"
    fi
fi

# 6. Instalare
echo "📦 Pas 3: Instalare APK..."
if [ "$DEVICE" = "waydroid" ]; then
    # Dezinstalam aplicatia pentru a forta o instalare curata si a evita cache-ul
    echo "   > Dezinstalare versiune veche..."
    waydroid shell pm uninstall "$PACKAGE_NAME" > /dev/null 2>&1 || true
    
    waydroid app install "$APK_PATH"
    echo "   > Instalare reusita pe Waydroid."
else
    # -r (reinstall), -d (allow downgrade)
    if adb -s "$DEVICE" install -r -d "$APK_PATH" >/dev/null 2>&1; then
        echo "   > Instalare reusita."
    else
        echo "   > Conflict detectat. Dezinstalare si reinstalare..."
        adb -s "$DEVICE" uninstall "$PACKAGE_NAME" >/dev/null 2>&1 || true
        adb -s "$DEVICE" install "$APK_PATH" >/dev/null 2>&1
        echo "   > Reinstalare reusita."
    fi
fi

# 7. Lansare
echo "🚀 Pas 4: Lansare aplicatie..."
if [ "$DEVICE" = "waydroid" ]; then
    # Ne asiguram ca containerul nu este inghetat (FROZEN)
    # Daca cere parola sudo, utilizatorul o va vedea in terminal
    if waydroid status | grep -q "FROZEN"; then
        echo "   > Container inghetat detectat. Incercare dezghetare..."
        sudo waydroid container unfreeze >/dev/null 2>&1 || true
    fi

    # Deschidem interfata grafica
    waydroid show-full-ui > /dev/null 2>&1 &
    sleep 2

    # Oprim instanta veche a aplicatiei pentru o pornire curata
    waydroid shell am force-stop "$PACKAGE_NAME" >/dev/null 2>&1 || true
    waydroid app launch "$PACKAGE_NAME"
else
    # Folosim monkey pentru a lansa fara a depinde de numele exact al MainActivity (care la MAUI e variabil)
    adb -s "$DEVICE" shell monkey -p "$PACKAGE_NAME" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
fi

echo "------------------------------------------------"
echo "✅ DEPLOY FINALIZAT CU SUCCES!"
echo "------------------------------------------------"
