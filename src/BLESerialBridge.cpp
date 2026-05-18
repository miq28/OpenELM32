#include "BLESerialBridge.h"
#include "ELM327_Emulator.h"

extern ELM327Emu elmEmulator;

static bool bleClientConnected = false;

class RXCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(
        NimBLECharacteristic *pCharacteristic,
        NimBLEConnInfo &connInfo) override
    {
        std::string value =
            pCharacteristic->getValue();

        for (size_t i = 0; i < value.length(); i++)
        {
            elmEmulator.processIncomingByte(
                (uint8_t)value[i]);
        }
    }
};

// ============================================================
// GLOBAL INSTANCE
// ============================================================

BLESerialBridge bleBridge;

// ============================================================
// SERVER CALLBACKS
// ============================================================

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(
        NimBLEServer *pServer,
        NimBLEConnInfo &connInfo) override
    {
        bleClientConnected = true;
    }

    void onDisconnect(
        NimBLEServer *pServer,
        NimBLEConnInfo &connInfo,
        int reason) override
    {
        bleClientConnected = false;

        NimBLEDevice::startAdvertising();
    }
};

// ============================================================
// BEGIN
// ============================================================

void BLESerialBridge::begin(const char *name)
{
    NimBLEDevice::init(name);

    NimBLEServer *server =
        NimBLEDevice::createServer();

    server->setCallbacks(new ServerCallbacks());

    NimBLEService *service =
        server->createService(
            "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

    // TX characteristic
    txChar = service->createCharacteristic(
        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
        NIMBLE_PROPERTY::NOTIFY);

    // RX characteristic
    NimBLECharacteristic *rxChar =
        service->createCharacteristic(
            "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
            NIMBLE_PROPERTY::WRITE |
                NIMBLE_PROPERTY::WRITE_NR);

    rxChar->setCallbacks(new RXCallbacks());

    service->start();

    NimBLEAdvertising *advertising =
        NimBLEDevice::getAdvertising();

    advertising->addServiceUUID(
        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

    advertising->start();
}

// ============================================================
// LOOP
// ============================================================

void BLESerialBridge::loop()
{
}

// ============================================================
// CONNECTED
// ============================================================

bool BLESerialBridge::connected()
{
    return bleClientConnected;
}

// ============================================================
// SEND
// ============================================================

void BLESerialBridge::send(
    const uint8_t *data,
    size_t len)
{
    if (!txChar)
    {
        return;
    }

    if (!bleClientConnected)
    {
        return;
    }

    txChar->setValue(data, len);

    txChar->notify();
}