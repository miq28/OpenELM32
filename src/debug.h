#pragma once
#include "rs485.h"
#include <Arduino.h>

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

// ===== RUNTIME CONTROL =====
inline bool debug_to_serial;   // kontrol Serial output

// ===== DELTA TIMESTAMP =====
static inline uint32_t dbg_delta_us()
{
    static uint32_t last = 0;
    uint32_t now = micros();
    uint32_t delta = now - last;
    last = now;
    return delta;
}

// ===== MAIN LOG (RS485 + optional Serial) =====
#define DEBUG(fmt, ...) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        DEBUGPORT.printf("+%lu|" fmt, _t, ##__VA_ARGS__); \
        if (debug_to_serial) \
            Serial.printf("+%lu|" fmt, _t, ##__VA_ARGS__); \
    } while (0)

// ===== HELPERS =====
#define DEBUG_PRINT(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        DEBUGPORT.printf("+%lu|%s", _t, (s)); \
        if (debug_to_serial) \
            Serial.printf("+%lu|%s", _t, (s)); \
    } while (0)

#define DEBUG_PRINTLN(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        DEBUGPORT.printf("+%lu|%s\n", _t, (s)); \
        if (debug_to_serial) \
            Serial.printf("+%lu|%s\n", _t, (s)); \
    } while (0)

#else

#define DEBUG(...)         do {} while (0)
#define DEBUG_PRINT(...)   do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)

#endif