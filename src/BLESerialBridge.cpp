#include "BLESerialBridge.h"
#include "ELM327_Emulator.h"
#include "debug.h"
#include <esp_heap_caps.h>

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

    // void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    //     // Fetch the incoming data as a std::string
    //     std::string rxValue = pCharacteristic->getValue();

    //     if (rxValue.length() > 0) {
    //         DEBUG("[BLE] Received %d bytes\n", rxValue.length());

    //         // Stream it straight out to your hardware Serial port
    //         for (int i = 0; i < rxValue.length(); i++) {
    //             Serial.print(rxValue[i]); 
    //         }
    //         Serial.println(); // Add line break for readability
            
    //         // OPTIONAL: If you want to parse the data inside your BLESerialBridge class,
    //         // you can feed it to a processing function here:
    //         // MyParser::processData(rxValue.c_str(), rxValue.length());
    //     }
    // }
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

    // void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    //     DEBUG("[BLE] Client Connected! Client Address: %s\n", 
    //           connInfo.getAddress().toString().c_str());
        
    //     // Optimizes connection speed for UART data transfer
    //     pServer->updateConnParams(connInfo.getConnHandle(), 12, 12, 0, 60);
    // }

    // void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    //     DEBUG("[BLE] Client Disconnected. Reason code: %d\n", reason);
        
    //     // CRITICAL: Restart advertising immediately so nRF Connect can find it again
    //     NimBLEDevice::startAdvertising();
    //     DEBUG("[BLE] Restarted advertising...\n");
    // }
};

// ============================================================
// BEGIN
// ============================================================
// void BLESerialBridge::begin(const char *name)
// {
//     NimBLEDevice::init(name);

//     NimBLEDevice::setDeviceName(name);

//     NimBLEDevice::setPower(ESP_PWR_LVL_P9);

//     DEBUG("[BLE] initialized=%d\n",
//           NimBLEDevice::isInitialized());

//     NimBLEDevice::setPower(ESP_PWR_LVL_P9);

//     NimBLEServer *server =
//         NimBLEDevice::createServer();

//     server->setCallbacks(new ServerCallbacks());

//     static const char *SERVICE_UUID =
//         "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";

//     static const char *TX_UUID =
//         "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

//     static const char *RX_UUID =
//         "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

//     NimBLEService *service =
//         server->createService(SERVICE_UUID);

//     txChar = service->createCharacteristic(
//         TX_UUID,
//         NIMBLE_PROPERTY::READ |
//             NIMBLE_PROPERTY::NOTIFY);

//     txChar->createDescriptor(
//         NimBLEUUID((uint16_t)0x2902));

//     NimBLECharacteristic *rxChar =
//         service->createCharacteristic(
//             RX_UUID,
//             NIMBLE_PROPERTY::WRITE |
//                 NIMBLE_PROPERTY::WRITE_NR);

//     rxChar->setCallbacks(new RXCallbacks());

//     server->start();

//     NimBLEAdvertising *advertising =
//         NimBLEDevice::getAdvertising();

//     advertising->reset();

//     advertising->setName(name);

//     advertising->addServiceUUID(SERVICE_UUID);

//     advertising->enableScanResponse(true);

//     advertising->setConnectableMode(true);

//     // advertising->setAdvertisementType(BLE_HS_ADV_TYPE_ADV_IND);

//     service->start();

//     DEBUG("[BLE] free heap before adv=%u\n",
//           ESP.getFreeHeap());

//     esp_err_t err = NimBLEDevice::startAdvertising();

//     bool ok = (err == ESP_OK);

//     DEBUG("[BLE] startAdvertising err=%d\n", err);

//     DEBUG("[BLE] free heap after adv=%u\n",
//           ESP.getFreeHeap());

//     if (ok)
//     {
//         DEBUG("[BLE] advertising started\n");

//         DEBUG("[BLE] initialized=%d\n",
//               NimBLEDevice::isInitialized());

//         DEBUG("[BLE] device count=%d\n",
//               NimBLEDevice::getCreatedClientCount());
//         DEBUG("[BLE] name=%s\n", name);
//         DEBUG("[BLE] addr=%s\n",
//               NimBLEDevice::getAddress().toString().c_str());
//     }
//     else
//     {
//         DEBUG("[BLE] advertising FAILED\n");

//         DEBUG("[BLE] device initialized=%d\n",
//               NimBLEDevice::isInitialized());
//     }
// }

void BLESerialBridge::begin(const char *name)
{
    NimBLEDevice::init(name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    DEBUG("[BLE] initialized=%d\n", NimBLEDevice::isInitialized());

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    static const char *TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
    static const char *RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

    NimBLEService *service = server->createService(SERVICE_UUID);

    txChar = service->createCharacteristic(
        TX_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rxChar = service->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new RXCallbacks());

    NimBLEAdvertising *advertising =
        NimBLEDevice::getAdvertising();

    advertising->setName(name);

    advertising->addServiceUUID(SERVICE_UUID);

    advertising->enableScanResponse(true);

    DEBUG("[BLE] free heap before adv=%u\n",
          ESP.getFreeHeap());

    bool ok = advertising->start();

    DEBUG("[BLE] advertising active=%d\n",
          advertising->isAdvertising());

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

// void BLESerialBridge::begin(const char *name)
// {
//     NimBLEDevice::init(name);
//     NimBLEDevice::setPower(ESP_PWR_LVL_P9);

//     DEBUG("[BLE] initialized=%d\n", NimBLEDevice::isInitialized());

//     NimBLEServer *server = NimBLEDevice::createServer();
//     server->setCallbacks(new ServerCallbacks());

//     static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
//     static const char *TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
//     static const char *RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

//     NimBLEService *service = server->createService(SERVICE_UUID);

//     txChar = service->createCharacteristic(
//         TX_UUID,
//         NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
//     );
//     // Note: NimBLE automatically creates the 0x2902 descriptor 
//     // when NIMBLE_PROPERTY::NOTIFY is set. You can safely remove this line.
//     // txChar->createDescriptor(NimBLEUUID((uint16_t)0x2902)); 

//     NimBLECharacteristic *rxChar = service->createCharacteristic(
//         RX_UUID,
//         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
//     );
//     rxChar->setCallbacks(new RXCallbacks());

//     // --- CRITICAL FIX 1: Start service BEFORE advertising ---
//     // service->start(); 

//     // --- CRITICAL FIX 2: Removed server->start() ---

//     NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
//     advertising->reset();
//     advertising->setName(name);
//     advertising->addServiceUUID(SERVICE_UUID);
//     advertising->enableScanResponse(true);
    
//     // Optional: Setting custom scan response data can sometimes fix nRF visibility
//     // advertising->setScanResponse(true); 

//     DEBUG("[BLE] free heap before adv=%u\n", ESP.getFreeHeap());

//     // --- CRITICAL FIX 3: Use the advertising instance directly ---
//     bool ok = advertising->start(); 

//     if (ok)
//     {
//         DEBUG("[BLE] advertising started\n");
//         DEBUG("[BLE] name=%s\n", name);
//         DEBUG("[BLE] addr=%s\n", NimBLEDevice::getAddress().toString().c_str());
//     }
//     else
//     {
//         DEBUG("[BLE] advertising FAILED\n");
//     }
// }


// void BLESerialBridge::begin(const char *name)
// {
//     NimBLEDevice::init(name);
//     NimBLEDevice::setPower(ESP_PWR_LVL_P9);

//     DEBUG("[BLE] initialized=%d\n", NimBLEDevice::isInitialized());

//     NimBLEServer *server = NimBLEDevice::createServer();
//     server->setCallbacks(new ServerCallbacks());

//     static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
//     static const char *TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
//     static const char *RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

//     NimBLEService *service = server->createService(SERVICE_UUID);

//     txChar = service->createCharacteristic(
//         TX_UUID,
//         NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
//     );
//     // Note: NimBLE automatically creates the 0x2902 descriptor 
//     // when NIMBLE_PROPERTY::NOTIFY is set. You can safely remove this line.
//     txChar->createDescriptor(NimBLEUUID((uint16_t)0x2902)); 

//     NimBLECharacteristic *rxChar = service->createCharacteristic(
//         RX_UUID,
//         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
//     );
//     rxChar->setCallbacks(new RXCallbacks());

//     // --- CRITICAL FIX 1: Start service BEFORE advertising ---
//     service->start(); 

//     // --- CRITICAL FIX 2: Removed server->start() ---

//     NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
//     advertising->reset();
//     advertising->setName(name);
//     advertising->addServiceUUID(SERVICE_UUID);
//     advertising->enableScanResponse(true);
    
//     // Optional: Setting custom scan response data can sometimes fix nRF visibility
//     // advertising->setScanResponse(true); 

//     DEBUG("[BLE] free heap before adv=%u\n", ESP.getFreeHeap());

//     // --- CRITICAL FIX 3: Use the advertising instance directly ---
//     bool ok = advertising->start(); 

//     if (ok)
//     {
//         DEBUG("[BLE] advertising started\n");
//         DEBUG("[BLE] name=%s\n", name);
//         DEBUG("[BLE] addr=%s\n", NimBLEDevice::getAddress().toString().c_str());
//     }
//     else
//     {
//         DEBUG("[BLE] advertising FAILED\n");
//     }
// }


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

    // if (txChar != nullptr) {
    //     // Set the value of the characteristic
    //     txChar->setValue(data, len);
        
    //     // Push the update out to the connected nRF Connect app
    //     txChar->notify(); 
    // }
}