// config.cpp

#include "config.h"
#include "console_io.h"

void printEEPROMSettings(const EEPROMSettings &cfg)
{
    consolePrintln();
    consolePrintln("========================================");
    consolePrintln("         EEPROM SETTINGS DUMP");
    consolePrintln("========================================");

    // ------------------------------------------------------------------
    // General
    // ------------------------------------------------------------------

    consolePrintf("useBinarySerialComm : %s\n",
                  cfg.useBinarySerialComm ? "true" : "false");
    consolePrintf("logLevel             : %u\n",
                  cfg.logLevel);
    consolePrintf("systemType           : %u\n",
                  cfg.systemType);

    // ------------------------------------------------------------------
    // Bluetooth
    // ------------------------------------------------------------------

    consolePrintf("enableBT             : %s\n",
                  cfg.enableBT ? "true" : "false");
    consolePrintf("btName               : %s\n",
                  cfg.btName);

    // ------------------------------------------------------------------
    // Bus
    // ------------------------------------------------------------------

    consolePrintf("sendingBus           : %d\n",
                  cfg.sendingBus);
    consolePrintf("enableLawicel        : %s\n",
                  cfg.enableLawicel ? "true" : "false");
    consolePrintf("enableLawicel        : %s\n",
                  cfg.enableVirtualOBD ? "true" : "false");

    // ------------------------------------------------------------------
    // WiFi
    // ------------------------------------------------------------------

    consolePrintf("wifiMode             : %u\n",
                  cfg.wifiMode);
    consolePrintf("STA_SSID             : %s\n",
                  cfg.STA_SSID);
    consolePrintf("STA_PASS             : %s\n",
                  cfg.STA_PASS);
    consolePrintf("AP_SSID              : %s\n",
                  cfg.AP_SSID);
    consolePrintf("AP_PASS              : %s\n",
                  cfg.AP_PASS);

    // ------------------------------------------------------------------
    // CAN buses
    // ------------------------------------------------------------------

    consolePrintln();
    consolePrintln("------------- CAN SETTINGS -------------");

    for (int i = 0; i < NUM_BUSES; i++)
    {
        const CANFDSettings &can = cfg.canSettings[i];

        consolePrintf("CAN BUS %d\n", i);
        consolePrintf("  nomSpeed    : %u\n",
                      can.nomSpeed);
        consolePrintf("  fdSpeed     : %u\n",
                      can.fdSpeed);
        consolePrintf("  enabled     : %s\n",
                      can.enabled ? "true" : "false");
        consolePrintf("  listenOnly  : %s\n",
                      can.listenOnly ? "true" : "false");
        consolePrintf("  fdMode      : %s\n",
                      can.fdMode ? "true" : "false");
        consolePrintln();
    }

    consolePrintln("========================================");
    consolePrintln();
}

void printSystemSettings(SystemSettings &cfg)
{
    consolePrintln();
    consolePrintln("========================================");
    consolePrintln("          SYSTEM SETTINGS DUMP");
    consolePrintln("========================================");

    // ------------------------------------------------------------------
    // LEDs
    // ------------------------------------------------------------------

    consolePrintf("LED_CANTX             : %u\n",
                  cfg.LED_CANTX);

    consolePrintf("LED_CANRX             : %u\n",
                  cfg.LED_CANRX);

    consolePrintf("LED_LOGGING           : %u\n",
                  cfg.LED_LOGGING);

    consolePrintf("LED_CONNECTION_STATUS : %u\n",
                  cfg.LED_CONNECTION_STATUS);

    consolePrintf("fancyLED              : %s\n",
                  cfg.fancyLED ? "true" : "false");

    // ------------------------------------------------------------------
    // LED toggles
    // ------------------------------------------------------------------

    consolePrintf("txToggle              : %s\n",
                  cfg.txToggle ? "true" : "false");

    consolePrintf("rxToggle              : %s\n",
                  cfg.rxToggle ? "true" : "false");

    consolePrintf("logToggle             : %s\n",
                  cfg.logToggle ? "true" : "false");

    // ------------------------------------------------------------------
    // LAWICEL
    // ------------------------------------------------------------------

    consolePrintf("lawicelMode           : %s\n",
                  cfg.lawicelMode ? "true" : "false");

    consolePrintf("lawicellExtendedMode  : %s\n",
                  cfg.lawicellExtendedMode ? "true" : "false");

    consolePrintf("lawicelAutoPoll       : %s\n",
                  cfg.lawicelAutoPoll ? "true" : "false");

    consolePrintf("lawicelTimestamping   : %s\n",
                  cfg.lawicelTimestamping ? "true" : "false");

    consolePrintf("lawicelPollCounter    : %d\n",
                  cfg.lawicelPollCounter);

    // ------------------------------------------------------------------
    // Bus reception flags
    // ------------------------------------------------------------------

    consolePrintln();
    consolePrintln("LAWICEL BUS RECEPTION:");

    for (int i = 0; i < NUM_BUSES; i++)
    {
        consolePrintf("  Bus %d : %s\n",
                      i,
                      cfg.lawicelBusReception[i] ? "enabled" : "disabled");
    }

    // ------------------------------------------------------------------
    // WiFi
    // ------------------------------------------------------------------

    consolePrintln();

    consolePrintf("isWifiConnected       : %s\n",
                  cfg.isWifiConnected ? "true" : "false");

    consolePrintf("isWifiActive          : %s\n",
                  cfg.isWifiActive ? "true" : "false");

    // ------------------------------------------------------------------
    // Hardware
    // ------------------------------------------------------------------

    consolePrintf("numBuses              : %d\n",
                  cfg.numBuses);

    // ------------------------------------------------------------------
    // WiFi clients
    // ------------------------------------------------------------------

    consolePrintln();
    consolePrintln("CLIENT CONNECTIONS:");

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        consolePrintf("clientNodes[%d]       : %s\n",
                      i,
                      cfg.clientNodes[i] ? "connected" : "disconnected");

        consolePrintf("wifiOBDClients[%d]    : %s\n",
                      i,
                      cfg.wifiOBDClients[i] ? "connected" : "disconnected");
    }

    consolePrintln("========================================");
    consolePrintln();
}