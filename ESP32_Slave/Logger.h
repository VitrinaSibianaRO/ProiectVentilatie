// Logger.h — Wrapper Serial cu nivele de log.
// Macros compile-out la LOG_LEVEL configurat in Config.h.
// Fara overhead in productie — zero apeluri de functie.
#pragma once
#include <Arduino.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

#ifndef LOG_LEVEL
  #define LOG_LEVEL LOG_LEVEL_WARN
#endif

class Logger {
public:
    static void begin(uint32_t baud) {
        Serial.begin(baud);
        delay(200);
    }

    static void log(int level, const char* tag, const char* fmt, ...) {
        if (level < LOG_LEVEL) return;
        char buf[200];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Serial.printf("[%lu][%s] %s\n", millis(), tag, buf);
    }
};

// Macros — se elimina complet la compile daca nivelul e prea mic.
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(...) Logger::log(LOG_LEVEL_DEBUG, "D", __VA_ARGS__)
#else
  #define LOG_DEBUG(...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
  #define LOG_INFO(...)  Logger::log(LOG_LEVEL_INFO,  "I", __VA_ARGS__)
#else
  #define LOG_INFO(...)  ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
  #define LOG_WARN(...)  Logger::log(LOG_LEVEL_WARN,  "W", __VA_ARGS__)
#else
  #define LOG_WARN(...)  ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
  #define LOG_ERROR(...) Logger::log(LOG_LEVEL_ERROR, "E", __VA_ARGS__)
#else
  #define LOG_ERROR(...) ((void)0)
#endif
