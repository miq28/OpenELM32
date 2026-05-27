/*
 * SerialConsole.cpp
 *
 Copyright (c) 2014-2018 Collin Kidder

 Shamelessly copied from the version in GEVCU

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "SerialConsole.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <esp32_can.h>
#include <Preferences.h>
#include "config.h"
#include "lawicel.h"
#include "ELM327_Emulator.h"
#include "can_manager.h"
#include "Logger.h"
#include "debug.h"
#include "console_io.h"
#include "ClassicBtElm327Server.h"
#include "openelm_identity.h"

extern void CANHandler();

static void applyUsbSerialBaud(uint32_t baud)
{
    Serial.flush();
    delay(50);
    while (Serial.available() > 0)
    {
        Serial.read();
    }
    Serial.begin(baud);
}

static void applyObdRuntimeProfile()
{
    settings.runtimeProfile = RUNTIME_PROFILE_OBD;
    settings.consoleCANOutput = false;
    settings.canStatsOutput = false;
    canManager.setSendToConsole(false);
    debug_enabled = true;
    debug_to_serial = false;
    debug_to_rs485 = true;
}

static void applyDevRuntimeProfile()
{
    settings.runtimeProfile = RUNTIME_PROFILE_DEV;
    settings.canStatsOutput = true;
    debug_enabled = true;
    debug_to_serial = false;
    debug_to_rs485 = true;
}

static void printRuntimeStatus()
{
    Logger::console("Runtime status:");
    Logger::console("  PROFILE=%s", settings.runtimeProfile == RUNTIME_PROFILE_OBD ? "OBD" : "DEV");
    Logger::console("  USB ELM=%s SERBAUD=%lu",
                    settings.enableElmSerial ? "ON" : "OFF",
                    (unsigned long)settings.serialBaud);
    Logger::console("  CLASSICBT=%i",
                    settings.enableClassicBt ? 1 : 0);
    Logger::console("  CONSOLECAN=%i CANSTAT=%i",
                    settings.consoleCANOutput ? 1 : 0,
                    settings.canStatsOutput ? 1 : 0);
    Logger::console("  ELMFASTPOLL=%i",
                    settings.elmFastPoll ? 1 : 0);
    Logger::console("  DEBUG=%i DEBUGSER=%i DEBUG485=%i",
                    debug_enabled ? 1 : 0,
                    debug_to_serial ? 1 : 0,
                    debug_to_rs485 ? 1 : 0);
    Logger::console("  LAWICEL=%i VIRTUALOBD=%i SENDBUS=%i",
                    settings.enableLawicel ? 1 : 0,
                    settings.enableVirtualOBD ? 1 : 0,
                    settings.sendingBus);
}

SerialConsole::SerialConsole()
{
    init();
}

void SerialConsole::init()
{
    // State variables for serial console
    ptrBuffer = 0;
    state = STATE_ROOT_MENU;
}

void SerialConsole::printMenu()
{
    char buff[80];
    // Show build # here as well in case people are using the native port and don't get to see the start up messages
    consolePrint("Build number: ");
    consolePrintln(CFG_BUILD_NUM);
    consolePrintln("System Menu:");
    consolePrintln();
    consolePrintln("Enable line endings of some sort (LF, CR, CRLF)");
    consolePrintln();
    consolePrintln("Short Commands:");
    consolePrintln("h = help (displays this message)");
    consolePrintln("i = status (shows current runtime mode)");
    consolePrintln("R = reset to factory defaults");
    consolePrintln("s = Start logging to file");
    consolePrintln("S = Stop logging to file");
    consolePrintln();
    consolePrintln("Config Commands (enter command=newvalue). Current values shown in parenthesis:");
    consolePrintln();

    Logger::console("SYSTYPE=%i - Set board type (0=Macchina A0, 1=EVTV ESP32 Board 2=Macchina A5)", settings.systemType);
    Logger::console("LOGLEVEL=%i - set log level (0=debug, 1=info, 2=warn, 3=error, 4=off)", settings.logLevel);
    consolePrintln();

    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        Logger::console("CANEN%i=%i - Enable/Disable CAN%i (0 = Disable, 1 = Enable)", i, settings.canSettings[i].enabled, i);
        Logger::console("CANSPEED%i=%i - Set speed of CAN%i in baud (125000, 250000, etc)", i, settings.canSettings[i].nomSpeed, i);
        if (canBuses[i]->supportsFDMode())
        {
            Logger::console("CANFDRATE%i=%i - FD Data Speed of CAN%i (500000, 2000000, etc)", i, settings.canSettings[i].fdSpeed, i);
            Logger::console("CANFDMODE%i=%i - Allow FD traffic on CAN%i (0 = Disable, 1 = Enable)", i, settings.canSettings[i].fdMode, i);
        }
        Logger::console("CANLISTENONLY%i=%i - Enable/Disable Listen Only Mode (0 = Dis, 1 = En)", i, settings.canSettings[i].listenOnly);
        consolePrintln();
        Logger::console("CANSEND%i=ID,LEN,<BYTES SEPARATED BY COMMAS> - Ex: CAN0SEND=0x200,4,1,2,3,4", i);
        consolePrintln();
    }

    // Logger::console("MARK=<Description of what you are doing> - Set a mark in the log file about what you are about to do.");
    // consolePrintln();

    Logger::console("BINSERIAL=%i - Enable/Disable Binary Sending of CANBus Frames to Serial (0=Dis, 1=En)", settings.useBinarySerialComm);
    Logger::console("SERBAUD=%lu - Set USB serial baud (115200, 1000000, etc)", (unsigned long)settings.serialBaud);
    Logger::console("CONSOLECAN=%i - Enable/Disable CAN frame output to USB serial (0=Dis, 1=En)", settings.consoleCANOutput);
    Logger::console("CANSTAT=%i - Enable/Disable periodic CAN stats output (0=Dis, 1=En)", settings.canStatsOutput);
    consolePrintln();

    Logger::console("BTNAME=%s - Set advertised Bluetooth name", settings.btName);
    Logger::console("SENDBUS=%i - Set which CAN bus to send messages from ELM327 emulator", settings.sendingBus);
    Logger::console("ELM327SERIAL=%i - Enable ELM327 command mode on USB serial (0=Dis, 1=En)", settings.enableElmSerial);
    Logger::console("CLASSICBT=%i - Enable Classic Bluetooth SPP on supported boards after reboot (0=Dis, 1=En)", settings.enableClassicBt);
    Logger::console("ELMFASTPOLL=%i - Return physical Mode 01 replies immediately (0=Dis, 1=En)", settings.elmFastPoll);
    Logger::console("APP=<preset> - Apply preset (OBD, SERIAL115200, SERIAL1000000, BTCLASSIC, DEV)");
    Logger::console("STATUS=1 - Show current runtime mode");
    Logger::console("RESETCONFIG=1 - Clear saved config; reboot applies defaults");
    consolePrintln();

    Logger::console("LAWICEL=%i - Set whether to accept LAWICEL commands (0 = Off, 1 = On)", settings.enableLawicel);
    consolePrintln();
    Logger::console("VIRTUALOBD=%i - Enable fake internal OBD ECU", settings.enableVirtualOBD);
    consolePrintln();

    Logger::console("DEBUG=%i - Enable runtime debug output", debug_enabled);
    Logger::console("DEBUGSER=%i - Debug output to USB serial", debug_to_serial);
    Logger::console("DEBUG485=%i - Debug output to RS485", debug_to_rs485);
    Logger::console("PROFILE=%s - Runtime profile (DEV or OBD)",
                    settings.runtimeProfile == RUNTIME_PROFILE_OBD ? "OBD" : "DEV");
    consolePrintln();

    Logger::console("WIFIMODE=%i - Set mode for WiFi (0 = Wifi Off, 1 = Connect to AP, 2 = Create AP", settings.wifiMode);
    Logger::console("SSID=%s - Set SSID to either connect to or create", (char *)settings.SSID);
    Logger::console("WPA2KEY=%s - Either passphrase or actual key", (char *)settings.WPA2Key);
}

/*	There is a help menu (press H or h or ?)
 This is no longer going to be a simple single character console.
 Now the system can handle up to 80 input characters. Commands are submitted
 by sending line ending (LF, CR, or both)
 */
void SerialConsole::rcvCharacter(uint8_t chr)
{
    if (chr == 10 || chr == 13)
    { // command done. Parse it.
        handleConsoleCmd();
        ptrBuffer = 0; // reset line counter once the line has been processed
    }
    else
    {
        cmdBuffer[ptrBuffer++] = (unsigned char)chr;
        if (ptrBuffer > 79)
            ptrBuffer = 79;
    }
}

void SerialConsole::handleConsoleCmd()
{
    if (state == STATE_ROOT_MENU)
    {
        if (ptrBuffer == 1)
        {
            // command is a single ascii character
            handleShortCmd();
        }
        else
        { // at least two bytes
            boolean equalSign = false;
            for (int i = 0; i < ptrBuffer; i++)
                if (cmdBuffer[i] == '=')
                    equalSign = true;
            cmdBuffer[ptrBuffer] = 0; // make sure to null terminate
            if (equalSign)
                handleConfigCmd();
            else if (settings.enableLawicel)
                lawicel.handleLongCmd(cmdBuffer);
        }
        ptrBuffer = 0; // reset line counter once the line has been processed
    }
}

void SerialConsole::handleShortCmd()
{
    uint8_t val;

    switch (cmdBuffer[0])
    {
    // non-lawicel commands
    case 'h':
    case '?':
    case 'H':
        printMenu();
        break;
    case 'i':
    case 'I':
        printRuntimeStatus();
        break;
    case 'R': // reset to factory defaults.
        prefs.begin(PREF_NAME, false);
        prefs.clear();
        prefs.end();
        Logger::console("Saved config cleared; power cycle to apply defaults");
        break;
    case '~':
        consolePrintln("DEBUGGING MODE!");
        CAN0.setDebuggingMode(true);
        // CAN1.setDebuggingMode(true);
        break;
    case '`':
        consolePrintln("Normal mode");
        CAN0.setDebuggingMode(false);
        // CAN1.setDebuggingMode(false);
        break;
    default:
        if (settings.enableLawicel)
            lawicel.handleShortCmd(cmdBuffer[0]);
        break;
    }
}

void SerialConsole::handleConfigCmd()
{
    int i;
    int newValue;
    char *newString;
    bool writeEEPROM = false;
    bool writeDigEE = false;
    char *dataTok;

    // Logger::debug("Cmd size: %i", ptrBuffer);
    if (ptrBuffer < 6)
        return;               // 4 digit command, =, value is at least 6 characters
    cmdBuffer[ptrBuffer] = 0; // make sure to null terminate
    String cmdString = String();
    unsigned char whichEntry = '0';
    i = 0;

    while (cmdBuffer[i] != '=' && i < ptrBuffer)
    {
        cmdString.concat(String(cmdBuffer[i++]));
    }
    i++; // skip the =
    if (i >= ptrBuffer)
    {
        Logger::console("Command needs a value..ie TORQ=3000");
        Logger::console("");
        return; // or, we could use this to display the parameter instead of setting
    }

    // strtol() is able to parse also hex values (e.g. a string "0xCAFE"), useful for enable/disable by device id
    newValue = strtol((char *)(cmdBuffer + i), NULL, 0); // try to turn the string into a number
    newString = (char *)(cmdBuffer + i);                 // leave it as a string

    cmdString.toUpperCase();

    if (cmdString.startsWith("CANEN"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        if (newValue < 0)
            newValue = 0;
        if (newValue > 1)
            newValue = 1;
        Logger::console("Setting CAN%i Enabled to %i", idx, newValue);
        settings.canSettings[idx].enabled = newValue;
        if (newValue == 1)
        {
            // CAN0.enable();
            canBuses[idx]->begin(settings.canSettings[idx].nomSpeed, 255);
            canBuses[idx]->watchFor();
        }
        else
            canBuses[idx]->disable();
        writeEEPROM = true;
    }
    else if (cmdString.startsWith("CANSPEED"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        if (newValue > 32000 && newValue <= 1000000)
        {
            Logger::console("Setting CAN%i Nominal Speed to %i", idx, newValue);

            settings.canSettings[idx].nomSpeed = newValue;

            char key[32];

            sprintf(key, "can%ispeed", idx);

            prefs.begin(PREF_NAME, false);
            prefs.putUInt(key, newValue);
            prefs.end();

            if (settings.canSettings[idx].enabled)
            {
                if (settings.canSettings[idx].fdMode)
                {
                    canBuses[idx]->begin(
                        settings.canSettings[idx].nomSpeed,
                        settings.canSettings[idx].fdSpeed);
                }
                else
                {
                    canBuses[idx]->begin(
                        settings.canSettings[idx].nomSpeed);
                }
            };
        }
        else
            Logger::console("Invalid baud rate! Enter a value 32000 - 1000000");
    }
    else if (cmdString.startsWith("CANFDRATE"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        if (canBuses[idx]->supportsFDMode())
        {
            if (newValue > 499999 && newValue <= 8000000)
            {
                Logger::console("Setting CAN%i FD Rate to %i", idx, newValue);
                settings.canSettings[idx].fdSpeed = newValue;
                if (settings.canSettings[idx].enabled)
                {
                    if (settings.canSettings[idx].fdMode)
                        canBuses[idx]->beginFD(settings.canSettings[idx].nomSpeed, settings.canSettings[idx].fdSpeed);
                }
                writeEEPROM = true;
            }
            else
                Logger::console("Invalid baud rate! Enter a value 500000 - 8000000");
        }
    }
    else if (cmdString.startsWith("CANFDMODE"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        if (canBuses[idx]->supportsFDMode())
        {
            if (newValue >= 0 && newValue <= 1)
            {
                Logger::console("Setting CAN%i FD Mode to %i", idx, newValue);
                settings.canSettings[idx].fdMode = newValue;
                if (settings.canSettings[idx].fdMode)
                    canBuses[idx]->beginFD(settings.canSettings[idx].nomSpeed, settings.canSettings[idx].fdSpeed);
                else
                    canBuses[idx]->begin(settings.canSettings[idx].nomSpeed, 255);
                writeEEPROM = true;
            }
            else
                Logger::console("Invalid setting! Enter a value 0 - 1");
        }
    }
    else if (cmdString.startsWith("CANLISTENONLY"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        if (newValue >= 0 && newValue <= 1)
        {
            Logger::console("Setting CAN%i Listen Only to %i", idx, newValue);
            settings.canSettings[idx].listenOnly = newValue;
            if (settings.canSettings[idx].listenOnly)
            {
                canBuses[idx]->setListenOnlyMode(true);
            }
            else
            {
                canBuses[idx]->setListenOnlyMode(false);
            }
            writeEEPROM = true;
        }
        else
            Logger::console("Invalid setting! Enter a value 0 - 1");
    }
    else if (cmdString == String("CAN0FILTER0"))
    { // someone should kick me in the face for this laziness... FIX THIS!
        handleFilterSet(0, 0, newString);
    }
    else if (cmdString == String("CAN0FILTER1"))
    {
        if (handleFilterSet(0, 1, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER2"))
    {
        if (handleFilterSet(0, 2, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER3"))
    {
        if (handleFilterSet(0, 3, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER4"))
    {
        if (handleFilterSet(0, 4, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER5"))
    {
        if (handleFilterSet(0, 5, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER6"))
    {
        if (handleFilterSet(0, 6, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN0FILTER7"))
    {
        if (handleFilterSet(0, 7, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER0"))
    {
        if (handleFilterSet(1, 0, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER1"))
    {
        if (handleFilterSet(1, 1, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER2"))
    {
        if (handleFilterSet(1, 2, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER3"))
    {
        if (handleFilterSet(1, 3, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER4"))
    {
        if (handleFilterSet(1, 4, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER5"))
    {
        if (handleFilterSet(1, 5, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER6"))
    {
        if (handleFilterSet(1, 6, newString))
            writeEEPROM = true;
    }
    else if (cmdString == String("CAN1FILTER7"))
    {
        if (handleFilterSet(1, 7, newString))
            writeEEPROM = true;
    }
    else if (cmdString.startsWith("CANSEND"))
    {
        int idx = cmdString[cmdString.length() - 1] - '0';
        if (idx < 0)
            idx = 0;
        if (idx > (SysSettings.numBuses - 1))
            idx = SysSettings.numBuses - 1;
        handleCANSend(*canBuses[idx], newString);
    }
    else if (cmdString == String("MARK"))
    { // just ascii based for now
        if (!settings.useBinarySerialComm)
            Logger::console("Mark: %s", newString);
    }
    else if (cmdString == String("BINSERIAL"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 1)
            newValue = 1;
        Logger::console("Setting Serial Binary Comm to %i", newValue);
        settings.useBinarySerialComm = newValue;
        writeEEPROM = true;
    }
    else if (cmdString == String("SERBAUD"))
    {
        if (newValue >= 9600 && newValue <= 2000000)
        {
            Logger::console("Setting USB serial baud to %i", newValue);
            settings.serialBaud = (uint32_t)newValue;
            writeEEPROM = true;
            applyUsbSerialBaud(settings.serialBaud);
        }
        else
        {
            Logger::console("Invalid USB baud rate! Enter a value 9600 - 2000000");
        }
    }
    else if (cmdString == String("CONSOLECAN"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 1)
            newValue = 1;
        Logger::console("Setting Console output of CAN to %i", newValue);
        settings.consoleCANOutput = (newValue != 0);
        canManager.setSendToConsole(newValue);
        writeEEPROM = true;
    }
    else if (cmdString == String("ELM327SERIAL"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 1)
            newValue = 1;

        settings.enableElmSerial = (newValue != 0);
        if (settings.enableElmSerial)
        {
            settings.consoleCANOutput = false;
            canManager.setSendToConsole(false);
            debug_to_serial = false;
        }
        else
        {
            canManager.setSendToConsole(settings.consoleCANOutput);
        }

        Logger::console("USB serial ELM327 mode %s",
                        settings.enableElmSerial ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("CLASSICBT") || cmdString == String("BTCLASSIC"))
    {
        settings.enableClassicBt = (newValue != 0);

#if OPENELM_CLASSIC_BT_SUPPORTED
        Logger::console("Classic Bluetooth SPP %s after reboot",
                        settings.enableClassicBt ? "ENABLED" : "DISABLED");
#else
        if (settings.enableClassicBt)
            Logger::console("Classic Bluetooth SPP requested, but this board target does not support Classic BT");
        else
            Logger::console("Classic Bluetooth SPP disabled");
#endif

        writeEEPROM = true;
    }
    else if (cmdString == String("SENDBUS"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 4)
            newValue = 4;
        Logger::console("Setting ELM327 sending bus to %i", newValue);
        settings.sendingBus = newValue;
        elmEmulator.setSendingBus(newValue);
        writeEEPROM = true;
    }
    else if (cmdString == String("LAWICEL"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 1)
            newValue = 1;
        Logger::console("Setting LAWICEL Mode to %i", newValue);
        settings.enableLawicel = newValue;
        writeEEPROM = true;
    }
    else if (cmdString == String("DEBUG"))
    {
        debug_enabled = (newValue != 0);

        Logger::console("Runtime debug %s",
                        debug_enabled ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("CANSTAT"))
    {
        settings.canStatsOutput = (newValue != 0);

        Logger::console("Periodic CAN stats %s",
                        settings.canStatsOutput ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("ELMFASTPOLL") || cmdString == String("ELM_FAST_POLL"))
    {
        settings.elmFastPoll = (newValue != 0);

        Logger::console("ELM fast polling %s",
                        settings.elmFastPoll ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("RESETCONFIG") || cmdString == String("FACTORYRESET"))
    {
        if (newValue == 1)
        {
            prefs.begin(PREF_NAME, false);
            prefs.clear();
            prefs.end();
            Logger::console("Saved config cleared; power cycle to apply defaults");
        }
        else
        {
            Logger::console("Use RESETCONFIG=1 to clear saved config");
        }
    }
    else if (cmdString == String("PROFILE"))
    {
        String profile = String(newString);
        profile.toUpperCase();

        if (profile == String("OBD") || profile == String("1"))
        {
            applyObdRuntimeProfile();
            Logger::console("Runtime profile set to OBD");
            writeEEPROM = true;
        }
        else if (profile == String("DEV") || profile == String("0"))
        {
            applyDevRuntimeProfile();
            Logger::console("Runtime profile set to DEV");
            writeEEPROM = true;
        }
        else
        {
            Logger::console("Invalid profile! Use PROFILE=OBD or PROFILE=DEV");
        }
    }
    else if (cmdString == String("APP"))
    {
        String app = String(newString);
        app.toUpperCase();

        if (app == String("OBD"))
        {
            settings.enableClassicBt = false;
            applyObdRuntimeProfile();
            Logger::console("App preset OBD: OBD quiet profile for app-facing transports, RS485 debug on");
            writeEEPROM = true;
        }
        else if (app == String("SERIAL115200") || app == String("SERIAL") ||
                 app == String("OBDAUTODOCTOR") || app == String("OBDAD") || app == String("AUTODOCTOR"))
        {
            settings.enableClassicBt = false;
            settings.enableElmSerial = true;
            settings.serialBaud = 115200;
            applyObdRuntimeProfile();
            debug_to_rs485 = false;
            Logger::console("App preset SERIAL115200: USB ELM327 at 115200, OBD quiet profile, RS485 debug off");
            writeEEPROM = true;
            applyUsbSerialBaud(settings.serialBaud);
        }
        else if (app == String("SERIAL1000000") || app == String("FASTSERIAL") || app == String("FAST"))
        {
            settings.enableClassicBt = false;
            settings.enableElmSerial = true;
            settings.serialBaud = 1000000;
            applyObdRuntimeProfile();
            Logger::console("App preset SERIAL1000000: USB ELM327 at 1000000, OBD quiet profile, RS485 debug on");
            writeEEPROM = true;
            applyUsbSerialBaud(settings.serialBaud);
        }
        else if (app == String("BTCLASSIC") || app == String("CLASSICBT") ||
                 app == String("MX") || app == String("MXPLUS"))
        {
            settings.enableClassicBt = true;
            settings.enableElmSerial = false;
            settings.wifiMode = 0;
            applyObdRuntimeProfile();
            Logger::console("App preset BTCLASSIC: Classic Bluetooth SPP as %s####, WiFi off after reboot",
                            OPENELM_CLASSIC_NAME_PREFIX);
            writeEEPROM = true;
        }
        else if (app == String("DEV"))
        {
            settings.enableClassicBt = false;
            settings.enableElmSerial = false;
            settings.serialBaud = 1000000;
            settings.consoleCANOutput = true;
            canManager.setSendToConsole(true);
            applyDevRuntimeProfile();
            Logger::console("App preset DEV: USB console at 1000000, CAN console on, RS485 debug on");
            writeEEPROM = true;
            applyUsbSerialBaud(settings.serialBaud);
        }
        else
        {
            Logger::console("Invalid app preset! Use APP=OBD, APP=SERIAL115200, APP=SERIAL1000000, APP=BTCLASSIC, or APP=DEV");
        }
    }
    else if (cmdString == String("STATUS"))
    {
        printRuntimeStatus();
    }
    else if (cmdString == String("DEBUGSER"))
    {
        debug_to_serial = (newValue != 0);

        Logger::console("USB serial debug %s",
                        debug_to_serial ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("DEBUG485"))
    {
        debug_to_rs485 = (newValue != 0);

        Logger::console("RS485 debug %s",
                        debug_to_rs485 ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else if (cmdString == String("WIFIMODE"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 2)
            newValue = 2;
        if (newValue == 0)
            Logger::console("Setting Wifi Mode to OFF");
        if (newValue == 1)
            Logger::console("Setting Wifi Mode to Connect to AP");
        if (newValue == 2)
            Logger::console("Setting Wifi Mode to Create AP");
        settings.wifiMode = newValue;
        writeEEPROM = true;
    }
    else if (cmdString == String("BTNAME"))
    {
        Logger::console("Setting Bluetooth Name to %s", newString);
        strcpy((char *)settings.btName, newString);
        writeEEPROM = true;
    }
    else if (cmdString == String("SSID"))
    {
        Logger::console("Setting SSID to %s", newString);
        strcpy((char *)settings.SSID, newString);
        writeEEPROM = true;
    }
    else if (cmdString == String("WPA2KEY"))
    {
        Logger::console("Setting WPA2 Key to %s", newString);
        strcpy((char *)settings.WPA2Key, newString);
        writeEEPROM = true;
    }
    else if (cmdString == String("SYSTYPE"))
    {
        if (newValue < 0)
            newValue = 0;
        if (newValue > 3)
            newValue = 3;
        if (newValue == 0)
            Logger::console("Setting board type to Macchina A0");
        if (newValue == 1)
            Logger::console("Setting board type to EVTV ESP32");
        if (newValue == 2)
            Logger::console("Setting board type to Macchina 5CAN");
        if (newValue == 3)
            Logger::console("Setting board type to EVTV ESP32-S3");
        settings.systemType = newValue;
        writeEEPROM = true;
    }
    else if (cmdString == String("LOGLEVEL"))
    {
        switch (newValue)
        {
        case 0:
            Logger::setLoglevel(Logger::Debug);
            settings.logLevel = 0;
            Logger::console("setting loglevel to 'debug'");
            writeEEPROM = true;
            break;
        case 1:
            Logger::setLoglevel(Logger::Info);
            settings.logLevel = 1;
            Logger::console("setting loglevel to 'info'");
            writeEEPROM = true;
            break;
        case 2:
            Logger::console("setting loglevel to 'warning'");
            settings.logLevel = 2;
            Logger::setLoglevel(Logger::Warn);
            writeEEPROM = true;
            break;
        case 3:
            Logger::console("setting loglevel to 'error'");
            settings.logLevel = 3;
            Logger::setLoglevel(Logger::Error);
            writeEEPROM = true;
            break;
        case 4:
            Logger::console("setting loglevel to 'off'");
            settings.logLevel = 4;
            Logger::setLoglevel(Logger::Off);
            writeEEPROM = true;
            break;
        }
    }
    else if (cmdString == String("VIRTUALOBD"))
    {
        settings.enableVirtualOBD = (newValue != 0);

        Logger::console("Virtual OBD %s",
                        settings.enableVirtualOBD ? "ENABLED" : "DISABLED");

        writeEEPROM = true;
    }
    else
    {
        Logger::console("Unknown command");
    }
    if (writeEEPROM)
    {
        prefs.begin(PREF_NAME, false);

        char buff[80];
        for (int i = 0; i < SysSettings.numBuses; i++)
        {
            sprintf(buff, "can%ispeed", i);
            prefs.putUInt(buff, settings.canSettings[i].nomSpeed);
            sprintf(buff, "can%i_en", i);
            prefs.putBool(buff, settings.canSettings[i].enabled);
            sprintf(buff, "can%i-listenonly", i);
            prefs.putBool(buff, settings.canSettings[i].listenOnly);
            sprintf(buff, "can%i-fdspeed", i);
            prefs.putUInt(buff, settings.canSettings[i].fdSpeed);
            sprintf(buff, "can%i-fdmode", i);
            prefs.putBool(buff, settings.canSettings[i].fdMode);
        }

        prefs.putBool("binarycomm", settings.useBinarySerialComm);
        prefs.putUInt("serialBaud", settings.serialBaud);
        prefs.putInt("sendingBus", settings.sendingBus);
        prefs.putBool("enableLawicel", settings.enableLawicel);
        prefs.putBool("elmSerial", settings.enableElmSerial);
        prefs.putBool("classicBt", settings.enableClassicBt);
        prefs.putBool("elmFastPoll", settings.elmFastPoll);
        prefs.putBool("consoleCAN", settings.consoleCANOutput);
        prefs.putBool("canStats", settings.canStatsOutput);

        prefs.putBool("dbg_en", debug_enabled);
        prefs.putBool("dbg_ser", debug_to_serial);
        prefs.putBool("dbg_485", debug_to_rs485);

        prefs.putUChar("loglevel", settings.logLevel);
        prefs.putUChar("runtimeProfile", settings.runtimeProfile);
        prefs.putUChar("systype", settings.systemType);
        prefs.putUChar("wifiMode", settings.wifiMode);
        prefs.putString("SSID", settings.SSID);
        prefs.putString("wpa2Key", settings.WPA2Key);
        prefs.putString("btname", settings.btName);
        prefs.putBool("virtualOBD", settings.enableVirtualOBD);
        prefs.end();
    }
}

// CAN0FILTER%i=%%i,%%i,%%i,%%i (ID, Mask, Extended, Enabled)", i);
bool SerialConsole::handleFilterSet(uint8_t bus, uint8_t filter, char *values)
{
    if (filter < 0 || filter > 7)
        return false;
    if (bus < 0 || bus > 1)
        return false;

    // there should be four tokens
    char *idTok = strtok(values, ",");
    char *maskTok = strtok(NULL, ",");
    char *extTok = strtok(NULL, ",");
    char *enTok = strtok(NULL, ",");

    if (!idTok)
        return false; // if any of them were null then something was wrong. Abort.
    if (!maskTok)
        return false;
    if (!extTok)
        return false;
    if (!enTok)
        return false;

    int idVal = strtol(idTok, NULL, 0);
    int maskVal = strtol(maskTok, NULL, 0);
    int extVal = strtol(extTok, NULL, 0);
    int enVal = strtol(enTok, NULL, 0);

    Logger::console("Setting CAN%iFILTER%i to ID 0x%x Mask 0x%x Extended %i Enabled %i", bus, filter, idVal, maskVal, extVal, enVal);

    if (bus == 0)
    {
        // settings.CAN0Filters[filter].id = idVal;
        // settings.CAN0Filters[filter].mask = maskVal;
        // settings.CAN0Filters[filter].extended = extVal;
        // settings.CAN0Filters[filter].enabled = enVal;
        // CAN0.setRXFilter(filter, idVal, maskVal, extVal);
    }
    else if (bus == 1)
    {
        // settings.CAN1Filters[filter].id = idVal;
        // settings.CAN1Filters[filter].mask = maskVal;
        // settings.CAN1Filters[filter].extended = extVal;
        // settings.CAN1Filters[filter].enabled = enVal;
        // CAN1.setRXFilter(filter, idVal, maskVal, extVal);
    }

    return true;
}

bool SerialConsole::handleCANSend(CAN_COMMON &port, char *inputString)
{
    char *idTok = strtok(inputString, ",");
    char *lenTok = strtok(NULL, ",");
    char *dataTok;
    CAN_FRAME frame;

    if (!idTok)
        return false;
    if (!lenTok)
        return false;

    int idVal = strtol(idTok, NULL, 0);
    int lenVal = strtol(lenTok, NULL, 0);

    for (int i = 0; i < lenVal; i++)
    {
        dataTok = strtok(NULL, ",");
        if (!dataTok)
            return false;
        frame.data.uint8[i] = strtol(dataTok, NULL, 0);
    }

    // things seem good so try to send the frame.
    frame.id = idVal;
    if (idVal >= 0x7FF)
        frame.extended = true;
    else
        frame.extended = false;
    frame.rtr = 0;
    frame.length = lenVal;
    port.sendFrame(frame);

    Logger::console("Sending frame with id: 0x%x len: %i", frame.id, frame.length);
    return true;
}

void SerialConsole::printBusName(int bus)
{
    switch (bus)
    {
    case 0:
        consolePrint("CAN0");
        break;
    case 1:
        consolePrint("CAN1");
        break;
    default:
        consolePrint("UNKNOWN");
        break;
    }
}
