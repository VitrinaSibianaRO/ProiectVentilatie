#pragma once

// ============================================================
//  OtaUpdater.h
//  OTA via GitHub releases cu SHA-256 streaming verification.
//
//  Flow:
//  1. Verifică URL whitelist (github.com / githubusercontent.com)
//  2. HTTPClient + WiFiClientSecure::setInsecure() (SHA-256 protejează)
//  3. Update.begin() → write chunks → Update.end()
//  4. SHA-256 calculat pe parcurs vs hash așteptat
//  5. Progress reporting via callback la fiecare 10%
//
//  Reguli:
//  - Funcție blocantă (nu se poate face non-blocking pe ESP32 Update)
//  - WDT trebuie alimentat manual în timpul download-ului
//  - Buffer download pe stack (4KB) — nu PSRAM pentru simplitate
// ============================================================

#include <Arduino.h>

// Callback pentru progress (0–100). Apelat la fiecare ~10%.
typedef void (*OtaProgressCallback)(int pct);

// Rezultat OTA
enum OtaResult {
    OTA_OK,
    OTA_ERR_URL_INVALID,
    OTA_ERR_CONNECT,
    OTA_ERR_HTTP,
    OTA_ERR_BEGIN,
    OTA_ERR_WRITE,
    OTA_ERR_END,
    OTA_ERR_SHA_MISMATCH
};

class OtaUpdater {
public:
    // Pornește update OTA. Funcție BLOCANTĂ — nu se întoarce
    // decât la succes (reboot) sau eroare.
    //
    // url: URL complet al firmware.bin (https://github.com/...)
    // expectedSha256: hash hex lowercase (64 chars)
    // progressCb: callback opțional pentru progress reporting
    //
    // Returnează OtaResult (OTA_OK doar teoretic — la succes face reboot).
    static OtaResult start(const char* url, const char* expectedSha256,
                           OtaProgressCallback progressCb = nullptr);

private:
    // Verifică dacă URL-ul e pe whitelist
    static bool _isUrlAllowed(const char* url);

    // Internal cu counter redirects pentru a evita loop infinit.
    static OtaResult _start(const char* url, const char* expectedSha256,
                            OtaProgressCallback progressCb,
                            int redirectDepth);
};
