#pragma once

#include <Arduino.h>

inline String buildObdlinkSerialNumber()
{
    uint64_t mac = ESP.getEfuseMac();
    uint16_t macSuffix = (uint16_t)(mac & 0xFFFF);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04u", (unsigned int)(macSuffix % 10000));

    String serial("23101234");
    serial += suffix;
    return serial;
}
