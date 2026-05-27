#include "ClassicBtElm327Server.h"

#include "console_io.h"
#include "debug.h"

static ClassicBtElm327Server* g_classicBtServer = nullptr;

ClassicBtElm327Server::ClassicBtElm327Server(ELM327Emu& emulator)
    : emulator(emulator),
      enabled(false),
      lastResponse("ELM327 v1.4b\r>")
{
    g_classicBtServer = this;
}

bool ClassicBtElm327Server::begin(const char* name)
{
#if OPENELM_CLASSIC_BT_SUPPORTED
    if (enabled)
    {
        return true;
    }

    if (!serial.begin(String(name), false, true))
    {
        consolePrintf("[BT] Classic SPP start failed\n");
        return false;
    }

    enabled = true;
    consolePrintf("[BT] Classic SPP advertising as %s\n", name);
    return true;
#else
    (void)name;
    consolePrintf("[BT] Classic SPP unsupported on this target\n");
    return false;
#endif
}

void ClassicBtElm327Server::loop()
{
#if OPENELM_CLASSIC_BT_SUPPORTED
    if (!enabled)
    {
        return;
    }

    int count = 0;
    while (serial.available() > 0 && count < 128)
    {
        int incoming = serial.read();
        if (incoming < 0)
        {
            return;
        }

        emulator.useClassicBtTransport();
        emulator.processIncomingByte((uint8_t)incoming);
        count++;
    }
#endif
}

void ClassicBtElm327Server::sendResponse(const String& response)
{
#if OPENELM_CLASSIC_BT_SUPPORTED
    lastResponse = response;

    if (!enabled)
    {
        return;
    }

    DEBUG("[BT ELM->APP TX] %s\n", printable(response).c_str());
    serial.write((const uint8_t*)response.c_str(), response.length());
#else
    (void)response;
#endif
}

ClassicBtElm327Server* ClassicBtElm327Server::getInstance()
{
    return g_classicBtServer;
}

String ClassicBtElm327Server::printable(String value)
{
    value.replace("\r", "\\r");
    value.replace("\n", "\\n");
    return value;
}
