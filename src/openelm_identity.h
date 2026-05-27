#pragma once

#include <Arduino.h>

#if __has_include("private_identity_overrides.h")
#include "private_identity_overrides.h"
#endif

#ifndef OPENELM_MODEL_NAME
#define OPENELM_MODEL_NAME "OpenELM32"
#endif

#ifndef OPENELM_CLASSIC_MODEL_NAME
#define OPENELM_CLASSIC_MODEL_NAME OPENELM_MODEL_NAME
#endif

#ifndef OPENELM_MANUFACTURER
#define OPENELM_MANUFACTURER "OpenELM Project"
#endif

#ifndef OPENELM_FIRMWARE_REVISION
#define OPENELM_FIRMWARE_REVISION "OpenELM32 1.0"
#endif

#ifndef OPENELM_SERIAL_PREFIX
#define OPENELM_SERIAL_PREFIX "OE32"
#endif

#ifndef OPENELM_CLASSIC_NAME_PREFIX
#define OPENELM_CLASSIC_NAME_PREFIX "OpenELM32 "
#endif

inline String buildOpenElmSerialSuffix()
{
    uint64_t mac = ESP.getEfuseMac();
    uint16_t macSuffix = (uint16_t)(mac & 0xFFFF);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04u", (unsigned int)(macSuffix % 10000));
    return String(suffix);
}

inline String buildOpenElmSerialNumber()
{
    String serial(OPENELM_SERIAL_PREFIX);
    serial += buildOpenElmSerialSuffix();
    return serial;
}

inline String buildOpenElmClassicName()
{
    String name(OPENELM_CLASSIC_NAME_PREFIX);
    name += buildOpenElmSerialSuffix();
    return name;
}
