#pragma once

#include <Arduino.h>
#include "debug.h"
#include "rs485.h"

// ===== PRINT =====

template<typename T>
static inline void consolePrint(const T &v)
{
    Serial.print(v);

    if (debug_to_rs485)
    {
        RS485.print(v);
    }
}

// ===== PRINTLN WITH VALUE =====

template<typename T>
static inline void consolePrintln(const T &v)
{
    Serial.println(v);

    if (debug_to_rs485)
    {
        RS485.println(v);
    }
}

// ===== EMPTY PRINTLN =====

static inline void consolePrintln()
{
    Serial.println();

    if (debug_to_rs485)
    {
        RS485.println("");
    }
}

// ===== PRINTF =====

static inline void consolePrintf(const char *fmt, ...)
{
    char buf[256];

    va_list args;

    va_start(args, fmt);

    vsnprintf(buf, sizeof(buf), fmt, args);

    va_end(args);

    Serial.print(buf);

    if (debug_to_rs485)
    {
        RS485.print(buf);
    }
}

// ===== WRITE SINGLE BYTE =====

static inline void consoleWrite(uint8_t c)
{
    Serial.write(c);

    if (debug_to_rs485)
    {
        RS485.write(c);
    }
}