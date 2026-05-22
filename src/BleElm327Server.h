#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "ELM327_Emulator.h"

class BleElm327Server {
public:
    BleElm327Server(ELM327Emu& emulator,
                    const char* deviceName = "OBDLink CX",
                    const char* manufacturer = "OBD Solutions, LLC",
                    const char* firmwareRevision = "5.6.19");

    void begin();
    void notifyResponse(const String& response);
    static BleElm327Server* getInstance();

private:
    class ServerCallbacks;
    class SerialCallbacks;

    ELM327Emu& emulator;
    const char* deviceName;
    const char* manufacturer;
    const char* firmwareRevision;

    NimBLECharacteristic* obdlinkNotifyChar = nullptr;
    NimBLECharacteristic* genericSerialChar = nullptr;
    bool clientConnected = false;
    String lastResponse;

    ServerCallbacks* serverCallbacks = nullptr;
    SerialCallbacks* serialCallbacks = nullptr;

    static String printable(String value);
    static NimBLECharacteristic* addDeviceInfoCharacteristic(NimBLEService* service, const char* uuid, const char* value);
};
