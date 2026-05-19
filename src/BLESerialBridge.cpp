#include "BLESerialBridge.h"
#include "ELM327_Emulator.h"
#include "debug.h"
#include "console_io.h"
#include <esp_heap_caps.h>

extern ELM327Emu elmEmulator;

static bool bleClientConnected = false;

void dumpHex(const char *prefix, const uint8_t *data, size_t len)
{
    consolePrintf("%s [%zu bytes]: ", prefix, len);
    for (size_t i = 0; i < len; i++)
    {
        consolePrintf("%02X ", data[i]);
    }
    consolePrintf(" | Text: ");
    for (size_t i = 0; i < len; i++)
    {
        char c = data[i];
        consolePrintf("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    consolePrintf("\n");
}

class RXCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        // Fetch the incoming data as a std::string
        std::string rxValue = pCharacteristic->getValue();

        if (rxValue.length() > 0)
        {
            // consolePrintf("[BLE] Received %d bytes, %s\n", rxValue.length(), rxValue.c_str());
            dumpHex("[BLE RX HEX]", (const uint8_t*)rxValue.data(), rxValue.length());

            for (size_t i = 0; i < rxValue.length(); i++)
            {
                elmEmulator.processIncomingByte((uint8_t)rxValue[i]);
                // consolePrint(rxValue[i]);
            }
            // consolePrintln(); // Add line break for readability

            // OPTIONAL: If you want to parse the data inside your BLESerialBridge class,
            // you can feed it to a processing function here:
            // MyParser::processData(rxValue.c_str(), rxValue.length());
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
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        bleClientConnected = true;

        DEBUG("[BLE] Client Connected! Client Address: %s\n",
              connInfo.getAddress().toString().c_str());

        // Optimizes connection speed for UART data transfer
        pServer->updateConnParams(connInfo.getConnHandle(), 12, 12, 0, 60);
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                      int reason) override
    {
        bleClientConnected = false;

        DEBUG("[BLE] Client Disconnected. Reason code: %d\n", reason);

        // CRITICAL: Restart advertising immediately so nRF Connect can find it again
        NimBLEDevice::startAdvertising();
        DEBUG("[BLE] Restarted advertising...\n");
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) override
    {
        DEBUG("[BLE] MTU changed to %u bytes\n", MTU);
    }
};

// ============================================================
// BEGIN
// ============================================================
void BLESerialBridge::begin(const char *name)
{
    NimBLEDevice::init(name);
    NimBLEDevice::setPower(9);

    DEBUG("[BLE] initialized=%d\n", NimBLEDevice::isInitialized());

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    static const char *SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
    static const char *RX_UUID = "0000fff1-0000-1000-8000-00805f9b34fb";
    static const char *TX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb";

    NimBLEService *service = server->createService(SERVICE_UUID);

    txChar = service->createCharacteristic(
        TX_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rxChar = service->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new RXCallbacks());

    service->start();

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->reset();

    // ============================================================
    // --- FIXED STRUCTURAL INJECTION ---
    // Inject the flags and the name into the same data object
    // ============================================================
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);

    // Adopting the GitHub structural approach: force the string name into the payload object
    advData.setName(name);

    // Pass the payload directly to the controller
    advertising->setAdvertisementData(advData);

    // Map your remaining automotive attributes
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->enableScanResponse(true);
    advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);

    bool ok = advertising->start(0);
    server->start();

    DEBUG("[BLE] free heap after adv=%u\n", ESP.getFreeHeap());
    DEBUG("[BLE] advertising active=%d\n", advertising->isAdvertising());

    if (ok)
    {
        DEBUG("[BLE] advertising started\n");
        DEBUG("[BLE] name=%s\n", name);
        DEBUG("[BLE] addr=%s\n", NimBLEDevice::getAddress().toString().c_str());
    }
    else
    {
        DEBUG("[BLE] advertising FAILED\n");
    }
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

void BLESerialBridge::sendData(const uint8_t *data, size_t len)
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

    // DEBUG("[BLE] Sent %u bytes\n", len);

    // // FIX: Explictly cap string printing to the precise 'len' size
    // consolePrintf("[BLE] Sent %zu bytes, %.*s\n", len, (int)len, (const char *)data);

    // Call the hex diagnostic tool
    dumpHex("[BLE TX HEX]", data, len);
}
