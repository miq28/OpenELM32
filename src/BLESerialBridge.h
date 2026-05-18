#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

class BLESerialBridge
{
public:
    void begin(const char *name);
    void loop();

    void send(const uint8_t *data, size_t len);
    bool connected();

private:
    NimBLECharacteristic *txChar = nullptr;
};

extern BLESerialBridge bleBridge;