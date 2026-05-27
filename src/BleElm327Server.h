#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "ELM327_Emulator.h"
#include "openelm_identity.h"

class BleElm327Server {
public:
    BleElm327Server(ELM327Emu& emulator,
                    const char* modelName = OPENELM_MODEL_NAME,
                    const char* manufacturer = OPENELM_MANUFACTURER,
                    const char* firmwareRevision = OPENELM_FIRMWARE_REVISION);

    void begin(const char* advertisedName = nullptr);
    void notifyResponse(const String& response);
    static BleElm327Server* getInstance();

private:
    enum class ReplyPath {
        PrimarySerial,
        GenericSerial,
    };

    class ServerCallbacks;
    class SerialCallbacks;
    class DeviceInfoCallbacks;

    ELM327Emu& emulator;
    const char* modelName;
    const char* manufacturer;
    const char* firmwareRevision;

    NimBLECharacteristic* primaryNotifyChar = nullptr;
    NimBLECharacteristic* genericSerialChar = nullptr;
    bool clientConnected = false;
    bool primaryNotifySubscribed = false;
    bool genericSerialSubscribed = false;
    uint16_t peerMtu = 23;
    String lastResponse;
    ReplyPath replyPath = ReplyPath::PrimarySerial;

    ServerCallbacks* serverCallbacks = nullptr;
    SerialCallbacks* serialCallbacks = nullptr;
    DeviceInfoCallbacks* deviceInfoCallbacks = nullptr;

    static String printable(String value);
    NimBLECharacteristic* addDeviceInfoCharacteristic(NimBLEService* service, const char* uuid, const char* value);
    void selectReplyPath(NimBLECharacteristic* characteristic);
    bool notifyPreferred(const String& response);
    void notifyChunked(NimBLECharacteristic* characteristic, const String& response, const char* label);
};
