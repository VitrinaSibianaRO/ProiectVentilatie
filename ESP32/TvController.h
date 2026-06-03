#pragma once

// ============================================================
//  TvController.h — LG 75XS4P via RS-232 over TCP (port 9761)
//  Controlat via WiFiClient pe acelasi LAN cu ESP32.
//  Power ON via Wake-on-LAN (UDP Magic Packet).
//  Polling la 6 minute (TV_POLL_MS din Config.h).
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "Config.h"

// ============================================================
//  TvState — toate datele citite de la TV
// ============================================================
struct TvState {
    bool     power           = false;
    uint8_t  volume          = 0;       // 0-100
    bool     muted           = false;
    uint8_t  inputId         = 0x90;    // 0x90=HDMI1, 0x91=HDMI2, 0xC0=DP
    int8_t   temperatureC    = 0;       // senzor intern panou
    bool     hasSignal       = false;   // HDMI primeste video
    uint8_t  pmMode          = 0;
    uint32_t usageHours      = 0;       // ore cumulate panou
    uint8_t  backlight       = 100;     // 0-100
    uint8_t  pictureMode     = 1;       // 0=Vivid,1=Std,2=Cinema,3=Sports,4=Game,9=Photos
    uint8_t  energySaving    = 0;       // 0=Off,1=Min,2=Med,3=Max,4=Auto,5=ScreenOff
    bool     noSignalPowerOff = false;
    char     serial[24]      = {};
    char     swVersion[16]   = {};
    bool     configured      = false;   // IP+MAC setate
    bool     reachable       = false;   // ultimul poll a reusit
};

// ============================================================
//  TvController
// ============================================================
class TvController {
public:
    TvController() {
        memset(_tvIp,  0, sizeof(_tvIp));
        memset(_tvMac, 0, sizeof(_tvMac));
    }

    // Apelat din setup(). Logica de alegere IP/MAC:
    //   1. Citeste config-ul setat din MAUI (NVS).
    //   2. Daca exista un IP hardcoded (TV_DEFAULT_IP) si TV-ul RASPUNDE acolo,
    //      foloseste hardcoded (IP + MAC).
    //   3. Altfel foloseste config-ul din MAUI.
    // Probe-ul are nevoie de WiFi activ — apelat dupa autoConnect in setup().
    void begin() {
        // 1. Config salvat din MAUI (NVS)
        char    nvsIp[16]  = {};
        uint8_t nvsMac[6]  = {};
        Preferences p;
        p.begin("tv", true);
        p.getString("ip",  nvsIp,  sizeof(nvsIp));
        p.getBytes("mac", nvsMac, sizeof(nvsMac));
        p.end();

        // 2. IP hardcoded are prioritate DOAR daca TV-ul raspunde acolo.
        bool useHardcoded = false;
        if (strlen(TV_DEFAULT_IP) > 0 && g_wifiAvailable) {
            strncpy(_tvIp, TV_DEFAULT_IP, sizeof(_tvIp) - 1);
            _tvIp[sizeof(_tvIp) - 1] = '\0';
            _state.configured = true;   // necesar pentru _send()
            char resp[32];
            if (_send("ka 01 ff", resp, sizeof(resp))) {
                useHardcoded = true;
                if (!parseMacString(TV_DEFAULT_MAC, _tvMac))
                    memcpy(_tvMac, nvsMac, 6);
            }
        }

        // 3. Fallback pe config-ul din MAUI
        if (!useHardcoded) {
            strncpy(_tvIp, nvsIp, sizeof(_tvIp) - 1);
            _tvIp[sizeof(_tvIp) - 1] = '\0';
            memcpy(_tvMac, nvsMac, 6);
            _state.configured = (strlen(_tvIp) > 0);
        }

        Serial.printf("[TV] begin: IP=%s sursa=%s\n",
                      _tvIp[0] ? _tvIp : "(none)",
                      useHardcoded ? "hardcoded" : "MAUI/NVS");
    }

    // Salveaza IP + MAC in NVS
    void configure(const char* ip, const uint8_t* mac) {
        strncpy(_tvIp, ip, sizeof(_tvIp) - 1);
        memcpy(_tvMac, mac, 6);
        Preferences p;
        p.begin("tv", false);
        p.putString("ip", _tvIp);
        p.putBytes("mac", _tvMac, 6);
        p.end();
        _state.configured = (strlen(_tvIp) > 0);
    }

    // Parseaza un MAC string "AA:BB:CC:DD:EE:FF" in bytes
    static bool parseMacString(const char* macStr, uint8_t* out) {
        unsigned int b[6] = {};
        if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
        return true;
    }

    // --------------------------------------------------------
    //  CONTROL — trimit comenzi TCP
    // --------------------------------------------------------

    // Power OFF via RS-232/TCP — nu poate porni TV oprit complet
    bool powerOff() {
        char resp[32];
        bool ok = _send("ka 01 00", resp, sizeof(resp));
        if (ok) _state.power = false;
        return ok;
    }

    // Power ON via Wake-on-LAN — fara confirmare, dar reflectam intentia in _state
    bool powerOn() {
        bool ok = _wol();
        if (ok) _state.power = true;
        return ok;
    }

    bool setVolume(uint8_t pct) {
        if (pct > 100) pct = 100;
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "kf 01 %02x", pct);
        char resp[32];
        bool ok = _send(cmd, resp, sizeof(resp));
        if (ok) _state.volume = pct;
        return ok;
    }

    bool setMute(bool muted) {
        char resp[32];
        bool ok = _send(muted ? "ke 01 00" : "ke 01 01", resp, sizeof(resp));
        if (ok) _state.muted = muted;
        return ok;
    }

    // inputCode: 0x90=HDMI1, 0x91=HDMI2, 0xC0=DP
    bool setInput(uint8_t inputCode) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "xb 01 %02x", inputCode);
        char resp[32];
        bool ok = _send(cmd, resp, sizeof(resp));
        if (ok) _state.inputId = inputCode;
        return ok;
    }

    bool setBacklight(uint8_t pct) {
        if (pct > 100) pct = 100;
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "mg 01 %02x", pct);
        char resp[32];
        bool ok = _send(cmd, resp, sizeof(resp));
        if (ok) _state.backlight = pct;
        return ok;
    }

    bool setPictureMode(uint8_t mode) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "dx 01 %02x", mode);
        char resp[32];
        bool ok = _send(cmd, resp, sizeof(resp));
        if (ok) _state.pictureMode = mode;
        return ok;
    }

    bool setEnergySaving(uint8_t level) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "jq 01 %02x", level);
        char resp[32];
        bool ok = _send(cmd, resp, sizeof(resp));
        if (ok) _state.energySaving = level;
        return ok;
    }

    bool setNoSignalPowerOff(bool enabled) {
        char resp[32];
        bool ok = _send(enabled ? "fg 01 01" : "fg 01 00", resp, sizeof(resp));
        if (ok) _state.noSignalPowerOff = enabled;
        return ok;
    }

    // --------------------------------------------------------
    //  POLLING — cadenta 6 minute
    // --------------------------------------------------------

    // Polling complet — apelat la fiecare TV_POLL_MS
    void pollAll() {
        if (!_state.configured) return;
        _state.reachable = false;

        uint8_t data;

        // Power status
        if (_query("ka", data)) {
            _state.power = (data == 0x01);
            _state.reachable = true;
        } else {
            return;   // TV inaccesibil — nu mai incercam
        }

        if (!_state.power) return;   // oprit — celelalte queries vor esua

        // Volum
        if (_query("kf", data)) _state.volume = data;

        // Mute
        if (_query("ke", data)) _state.muted = (data == 0x00);

        // Input
        if (_query("xb", data)) _state.inputId = data;

        // Temperatura interna (hex byte = valoare in celsius)
        if (_query("dn", data)) _state.temperatureC = (int8_t)data;

        // Status semnal video (subcommand 02)
        {
            char resp[32];
            if (_send("sv 01 02", resp, sizeof(resp))) {
                uint8_t d;
                if (_parseDataByte(resp, 'v', d)) _state.hasSignal = (d != 0x00);
            }
        }

        // Ore utilizare panou
        {
            char resp[32];
            if (_send("dl 01 ff", resp, sizeof(resp))) {
                uint8_t d;
                if (_parseDataByte(resp, 'l', d)) _state.usageHours = d;
            }
        }

        // Backlight
        if (_query("mg", data)) _state.backlight = data;

        // Picture mode
        if (_query("dx", data)) _state.pictureMode = data;

        // Energy saving
        if (_query("jq", data)) _state.energySaving = data;

        // No Signal Power Off
        if (_query("fg", data)) _state.noSignalPowerOff = (data != 0x00);
    }

    // Citeste serial + sw version — o singura data la boot
    void readDeviceInfo() {
        if (!_state.configured) return;

        char resp[64];
        // Serial number: fy 01 ff — raspuns ASCII in data field
        if (_send("fy 01 ff", resp, sizeof(resp))) {
            _extractAsciiData(resp, _state.serial, sizeof(_state.serial));
        }
        // Software version: fz 01 ff
        if (_send("fz 01 ff", resp, sizeof(resp))) {
            _extractAsciiData(resp, _state.swVersion, sizeof(_state.swVersion));
        }
    }

    TvState& state() { return _state; }
    const TvState& state() const { return _state; }

    const char* ip() const { return _tvIp; }

private:
    char     _tvIp[16];
    uint8_t  _tvMac[6];
    TvState  _state;

    // --------------------------------------------------------
    //  Trimite o comanda RS-232/TCP si citeste raspunsul
    //  Formatul comenzii: "xx 01 yy" (cmd1cmd2 setId data)
    //  Raspuns asteptat:  "x2 01 OKdatax\r" sau "x2 01 NGx\r"
    // --------------------------------------------------------
    bool _send(const char* cmd, char* respBuf, size_t bufLen,
               uint32_t timeoutMs = TV_TCP_TIMEOUT_MS) {
        if (!_state.configured || strlen(_tvIp) == 0) return false;

        WiFiClient client;
        // Al treilea parametru = timeout connect in ms (ESP32 Arduino 3.x).
        // Fara el, connect() blocheaza 10-30s per incercare esuata → WDT panic.
        if (!client.connect(_tvIp, TV_TCP_PORT, (int32_t)timeoutMs)) return false;

        // Trimitere comanda cu CR terminator
        char fullCmd[64];
        snprintf(fullCmd, sizeof(fullCmd), "%s\r", cmd);
        client.print(fullCmd);

        // Citire raspuns pana la '\r' sau timeout
        uint32_t start = millis();
        size_t idx = 0;
        while (millis() - start < timeoutMs) {
            if (client.available()) {
                char c = client.read();
                if (c == '\r' || c == '\n') break;
                if (idx < bufLen - 1) respBuf[idx++] = c;
            }
            delay(5);
        }
        respBuf[idx] = '\0';
        client.stop();

        return (idx > 0);
    }

    // Query cu DATA=ff, parseaza si returneaza byte-ul de date
    bool _query(const char* cmd2Char, uint8_t& dataOut) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "%s 01 ff", cmd2Char);
        char resp[32];
        if (!_send(cmd, resp, sizeof(resp))) return false;
        return _parseDataByte(resp, cmd2Char[1], dataOut);
    }

    // Parseaza "x2 01 OKdatax" — extrage byte hex din "data"
    // Formatul: CMD2 SP SETID SP "OK" DATA "x"
    // Exemplu: "a 01 OK01x" pentru power=on
    bool _parseDataByte(const char* resp, char cmd2, uint8_t& dataOut) {
        // Cauta "OK" in raspuns
        const char* ok = strstr(resp, "OK");
        if (!ok) return false;
        const char* dataStart = ok + 2;
        // Citeste 2 caractere hex
        char hex[3] = {0};
        strncpy(hex, dataStart, 2);
        char* endPtr;
        unsigned long val = strtoul(hex, &endPtr, 16);
        if (endPtr == hex) return false;
        dataOut = (uint8_t)val;
        return true;
    }

    // Extrage date ASCII din raspuns (pentru serial/version)
    void _extractAsciiData(const char* resp, char* out, size_t outLen) {
        const char* ok = strstr(resp, "OK");
        if (!ok) { out[0] = '\0'; return; }
        const char* dataStart = ok + 2;
        size_t len = strlen(dataStart);
        // Elimina 'x' final
        if (len > 0 && dataStart[len - 1] == 'x') len--;
        if (len >= outLen) len = outLen - 1;
        strncpy(out, dataStart, len);
        out[len] = '\0';
    }

    // Wake-on-LAN — UDP Magic Packet la broadcast LAN
    bool _wol() {
        // Verifica ca avem un MAC setat (non-zero)
        bool hasMac = false;
        for (int i = 0; i < 6; i++) if (_tvMac[i] != 0) { hasMac = true; break; }
        if (!hasMac) return false;

        // Magic Packet: 6 x 0xFF + 16 x MAC
        uint8_t packet[102];
        memset(packet, 0xFF, 6);
        for (int i = 0; i < 16; i++) memcpy(packet + 6 + i * 6, _tvMac, 6);

        WiFiUDP udp;
        udp.begin(0);
        // Broadcast la 255.255.255.255 port 9 (WoL standard)
        udp.beginPacket(IPAddress(255, 255, 255, 255), 9);
        udp.write(packet, sizeof(packet));
        return udp.endPacket() == 1;
    }
};
