#pragma once

#include <Arduino.h>
#include "ELM327_Emulator.h"

#if defined(WEACT_STUDIO_CAN485_V1)
#define OPENELM_CLASSIC_BT_SUPPORTED 1
#else
#define OPENELM_CLASSIC_BT_SUPPORTED 0
#endif

#if OPENELM_CLASSIC_BT_SUPPORTED
#include <BluetoothSerial.h>
#endif

class ClassicBtElm327Server
{
public:
    explicit ClassicBtElm327Server(ELM327Emu& emulator);

    bool begin(const char* name);
    void loop();
    void sendResponse(const String& response);
    bool isEnabled() const { return enabled; }

    static ClassicBtElm327Server* getInstance();

private:
    static String printable(String value);

    ELM327Emu& emulator;
    bool enabled;
    String lastResponse;

#if OPENELM_CLASSIC_BT_SUPPORTED
    BluetoothSerial serial;
#endif
};
