#pragma once
#include "rs485.h"
#include <Arduino.h>

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

inline bool debug_enabled = true;
inline bool debug_to_serial = false;
inline bool debug_to_rs485 = true;

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
#define DEBUG(fmt, ...)                  \
    do                                   \
    {                                    \
        if (debug_enabled)               \
        {                                \
            if (debug_to_serial)         \
                Serial.printf(fmt, ##__VA_ARGS__); \
            if (debug_to_rs485)          \
                RS485.printf(fmt, ##__VA_ARGS__); \
        }                                \
    } while (0)

#define DEBUG_PRINT(x)                   \
    do                                   \
    {                                    \
        if (debug_enabled)               \
        {                                \
            if (debug_to_serial)         \
                Serial.print(x);         \
            if (debug_to_rs485)          \
                RS485.print(x);          \
        }                                \
    } while (0)

#define DEBUG_PRINTLN(x)                 \
    do                                   \
    {                                    \
        if (debug_enabled)               \
        {                                \
            if (debug_to_serial)         \
                Serial.println(x);       \
            if (debug_to_rs485)          \
                RS485.println(x);        \
        }                                \
    } while (0)

#else

#define DEBUG(...) \
    do             \
    {              \
    } while (0)
#define DEBUG_PRINT(...) \
    do                   \
    {                    \
    } while (0)
#define DEBUG_PRINTLN(...) \
    do                     \
    {                      \
    } while (0)

#endif