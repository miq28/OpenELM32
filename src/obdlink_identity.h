#pragma once

#include <Arduino.h>

inline String buildObdlinkSerialSuffix()
{
    uint64_t mac = ESP.getEfuseMac();
    uint16_t macSuffix = (uint16_t)(mac & 0xFFFF);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04u", (unsigned int)(macSuffix % 10000));
    return String(suffix);
}

inline String buildObdlinkSerialNumber()
{
    String serial("23101234");
    serial += buildObdlinkSerialSuffix();
    return serial;
}

inline String buildObdlinkMxClassicName()
{
    String name("OBDLink MX+ ");
    name += buildObdlinkSerialSuffix();
    return name;
}
