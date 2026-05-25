#include "BleElm327Server.h"
#include "debug.h"
#include "console_io.h"

static BleElm327Server* g_bleServer = nullptr;

class BleElm327Server::ServerCallbacks : public NimBLEServerCallbacks {
public:
    explicit ServerCallbacks(BleElm327Server& owner) : owner(owner) {
    }

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        owner.clientConnected = true;
        owner.peerMtu = connInfo.getMTU();
        consolePrintf("[BLE] Client connected. Address: %s, ID Address: %s, Handle: %u\n",
                      connInfo.getAddress().toString().c_str(),
                      connInfo.getIdAddress().toString().c_str(),
                      connInfo.getConnHandle());
        consolePrintf("[BLE] Connected clients: %u/%u\n",
                      pServer->getConnectedCount(),
                      CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
        consolePrintf("[BLE] Connection details:\n%s\n", connInfo.toString().c_str());

        int rc = 0;
        if (!connInfo.isEncrypted() &&
            !NimBLEDevice::startSecurity(connInfo.getConnHandle(), &rc)) {
            consolePrintf("[BLE] Failed to start pairing/security: %d (%s)\n",
                          rc,
                          NimBLEUtils::returnCodeToString(rc));
        }

        if (pServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            if (NimBLEDevice::startAdvertising()) {
                consolePrintln("[BLE] Advertising kept active for another client");
            } else {
                consolePrintln("[BLE] Failed to keep advertising active after connect");
            }
        }
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        owner.clientConnected = pServer->getConnectedCount() > 0;
        owner.obdlinkNotifySubscribed = false;
        owner.genericSerialSubscribed = false;
        owner.peerMtu = 23;
        consolePrintf("[BLE] Client disconnected. Reason: %d (%s). Connected clients: %u/%u\n",
                      reason,
                      NimBLEUtils::returnCodeToString(reason),
                      pServer->getConnectedCount(),
                      CONFIG_BT_NIMBLE_MAX_CONNECTIONS);

        if (pServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
            if (NimBLEDevice::startAdvertising()) {
                consolePrintln("[BLE] Advertising active");
            } else {
                consolePrintln("[BLE] Failed to restart advertising");
            }
        }
    }

    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
        owner.peerMtu = mtu;
        consolePrintf("[BLE] MTU updated: %u for handle: %u\n", mtu, connInfo.getConnHandle());
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        consolePrintf("[BLE] Authentication complete: bonded=%s encrypted=%s authenticated=%s keySize=%u address=%s\n",
                      connInfo.isBonded() ? "yes" : "no",
                      connInfo.isEncrypted() ? "yes" : "no",
                      connInfo.isAuthenticated() ? "yes" : "no",
                      connInfo.getSecKeySize(),
                      connInfo.getAddress().toString().c_str());
    }

private:
    BleElm327Server& owner;
};

class BleElm327Server::SerialCallbacks : public NimBLECharacteristicCallbacks {
public:
    explicit SerialCallbacks(BleElm327Server& owner) : owner(owner) {
    }

    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        characteristic->setValue(owner.lastResponse);
        consolePrintf("[BLE] Read %s: %s\n",
                      characteristic->getUUID().toString().c_str(),
                      printable(owner.lastResponse).c_str());
    }

    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        String data = characteristic->getValue();
        consolePrintf("[BLE] Write %s: %s\n",
                      characteristic->getUUID().toString().c_str(),
                      printable(data).c_str());

        owner.emulator.useBLETransport();

        // Feed bytes to emulator byte-by-byte
        for (size_t i = 0; i < data.length(); i++) {
            owner.emulator.processIncomingByte((uint8_t)data[i]);
        }
    }

    void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        const bool subscribed = subValue != 0;

        if (characteristic == owner.obdlinkNotifyChar) {
            owner.obdlinkNotifySubscribed = subscribed;
        } else if (characteristic == owner.genericSerialChar) {
            owner.genericSerialSubscribed = subscribed;
        }

        consolePrintf("[BLE] Subscribe %s: %s\n",
                      characteristic->getUUID().toString().c_str(),
                      subscribed ? "on" : "off");
    }

private:
    BleElm327Server& owner;

    static String printable(String value) {
        return BleElm327Server::printable(value);
    }
};

class BleElm327Server::DeviceInfoCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        String value = characteristic->getValue();
        consolePrintf("[BLE] DeviceInfo read %s: %s\n",
                      characteristic->getUUID().toString().c_str(),
                      BleElm327Server::printable(value).c_str());
    }
};

BleElm327Server::BleElm327Server(ELM327Emu& emulator,
                                 const char* modelName,
                                 const char* manufacturer,
                                 const char* firmwareRevision)
    : emulator(emulator),
      modelName(modelName),
      manufacturer(manufacturer),
      firmwareRevision(firmwareRevision),
      lastResponse("ELM327 v1.5\r\r>") {
}

void BleElm327Server::begin(const char* advertisedName) {
    g_bleServer = this;
    const char* bleName = advertisedName && advertisedName[0] ? advertisedName : modelName;

    serverCallbacks = new ServerCallbacks(*this);
    serialCallbacks = new SerialCallbacks(*this);
    deviceInfoCallbacks = new DeviceInfoCallbacks();

    NimBLEDevice::init(modelName);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);
    consolePrintf("[BLE] Stored bonds: %u\n", NimBLEDevice::getNumBonds());

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(serverCallbacks);

    NimBLEService* deviceInfo = server->createService("180A");
    addDeviceInfoCharacteristic(deviceInfo, "2A29", manufacturer);
    addDeviceInfoCharacteristic(deviceInfo, "2A24", modelName);
    addDeviceInfoCharacteristic(deviceInfo, "2A25", "231012345678");
    addDeviceInfoCharacteristic(deviceInfo, "2A26", firmwareRevision);
    addDeviceInfoCharacteristic(deviceInfo, "2A27", "r1.0.0");
    addDeviceInfoCharacteristic(deviceInfo, "2A28", "2024.02.01");

    NimBLEService* obdlinkService = server->createService("0000FFF0-0000-1000-8000-00805F9B34FB");
    obdlinkNotifyChar = obdlinkService->createCharacteristic(
        "0000FFF1-0000-1000-8000-00805F9B34FB",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    obdlinkNotifyChar->setCallbacks(serialCallbacks);
    obdlinkNotifyChar->setValue(lastResponse);

    NimBLECharacteristic* obdlinkWriteChar = obdlinkService->createCharacteristic(
        "0000FFF2-0000-1000-8000-00805F9B34FB",
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    obdlinkWriteChar->setCallbacks(serialCallbacks);

    NimBLEService* genericService = server->createService("0000FFE0-0000-1000-8000-00805F9B34FB");
    genericSerialChar = genericService->createCharacteristic(
        "0000FFE1-0000-1000-8000-00805F9B34FB",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
    );
    genericSerialChar->setCallbacks(serialCallbacks);
    genericSerialChar->setValue(lastResponse);

    server->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->reset();

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setCompleteServices16({NimBLEUUID((uint16_t)0xFFF0), NimBLEUUID((uint16_t)0xFFE0)});

    NimBLEAdvertisementData scanData;
    scanData.setName(bleName);

    advertising->setAdvertisementData(advData);
    advertising->setScanResponseData(scanData);
    advertising->enableScanResponse(true);
    advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);

    if (advertising->start(0)) {
        consolePrintf("[BOOT] Advertising as %s (model %s)\n", bleName, modelName);
    } else {
        consolePrintln("[BOOT] Failed to start advertising");
    }
}

void BleElm327Server::notifyResponse(const String& response) {
    lastResponse = response;

    bool notified = false;

    if (obdlinkNotifySubscribed) {
        notifyChunked(obdlinkNotifyChar, response, "FFF1");
        notified = true;
    }

    if (genericSerialSubscribed) {
        notifyChunked(genericSerialChar, response, "FFE1");
        notified = true;
    }

    if (!notified) {
        consolePrintln("[BLE] notify skipped: no subscribed ELM characteristic");
    }

}

void BleElm327Server::notifyChunked(NimBLECharacteristic* characteristic,
                                    const String& response,
                                    const char* label) {
    if (characteristic == nullptr) {
        return;
    }

    size_t maxChunkLen = peerMtu > 3 ? peerMtu - 3 : 20;
    if (maxChunkLen < 20) {
        maxChunkLen = 20;
    }
    size_t offset = 0;
    size_t chunks = 0;

    while (offset < response.length()) {
        const size_t remaining = response.length() - offset;
        const size_t chunkLen = remaining > maxChunkLen ? maxChunkLen : remaining;
        String chunk = response.substring(offset, offset + chunkLen);

        characteristic->setValue(chunk);
        characteristic->notify();

        offset += chunkLen;
        chunks++;

        if (offset < response.length()) {
            delay(3);
        }
    }

    consolePrintf("[BLE] %s notify: %zu bytes in %zu chunk(s)\n",
                  label,
                  response.length(),
                  chunks);
}

String BleElm327Server::printable(String value) {
    value.replace("\r", "\\r");
    value.replace("\n", "\\n");
    return value;
}

NimBLECharacteristic* BleElm327Server::addDeviceInfoCharacteristic(NimBLEService* service,
                                                                   const char* uuid,
                                                                   const char* value) {
    NimBLECharacteristic* characteristic = service->createCharacteristic(uuid, NIMBLE_PROPERTY::READ);
    characteristic->setValue(value);
    characteristic->setCallbacks(deviceInfoCallbacks);
    return characteristic;
}

BleElm327Server* BleElm327Server::getInstance() {
    return g_bleServer;
}
