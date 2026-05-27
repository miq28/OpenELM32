/*
 *  ELM327_Emu.cpp
 *
 * Class emulates the serial comm of an ELM327 chip - Used to create an OBDII interface
 *
 * Created: 3/23/2017
 *  Author: Collin Kidder
 */

/*
 Copyright (c) 2017 Collin Kidder

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

#include "ELM327_Emulator.h"
#include "config.h"
#include "Logger.h"
#include "utility.h"
#include "esp32_can.h"
#include "can_manager.h"
#include "debug.h"
#include "BleElm327Server.h"
#include "ClassicBtElm327Server.h"
#include "openelm_identity.h"

static uint32_t elmTxnCounter = 0;

static String escapeElmLogText(const char *data, size_t len)
{
    String out;
    char buff[5];

    for (size_t i = 0; i < len; i++)
    {
        uint8_t value = (uint8_t)data[i];

        if (value == '\r')
        {
            out += "\\r";
        }
        else if (value == '\n')
        {
            out += "\\n";
        }
        else if (value == '\t')
        {
            out += "\\t";
        }
        else if (value >= 0x20 && value <= 0x7E)
        {
            out += (char)value;
        }
        else
        {
            sprintf(buff, "\\x%02X", value);
            out += buff;
        }
    }

    return out;
}

static String escapeElmLogText(const String &data)
{
    return escapeElmLogText(data.c_str(), data.length());
}

static String formatCanFrameData(const CAN_FRAME &frame)
{
    String out;
    char buff[4];

    for (int i = 0; i < frame.length; i++)
    {
        if (i > 0)
        {
            out += ' ';
        }

        sprintf(buff, "%02X", frame.data.uint8[i]);
        out += buff;
    }

    return out;
}

static void appendSuffix(String &out, const char *source, uint8_t count)
{
    size_t sourceLen = strlen(source);
    if (count > sourceLen)
    {
        count = sourceLen;
    }

    out += source + (sourceLen - count);
}

static String resolveBluetoothNameTemplate(const char *nameTemplate)
{
    String serialNumber = buildOpenElmSerialNumber();
    uint64_t mac = ESP.getEfuseMac();
    char macHex[13];
    sprintf(macHex,
            "%02X%02X%02X%02X%02X%02X",
            (uint8_t)(mac >> 40),
            (uint8_t)(mac >> 32),
            (uint8_t)(mac >> 24),
            (uint8_t)(mac >> 16),
            (uint8_t)(mac >> 8),
            (uint8_t)(mac >> 0));

    String resolved;

    for (size_t i = 0; nameTemplate[i] != 0; i++)
    {
        if (nameTemplate[i] == '%' && isdigit((unsigned char)nameTemplate[i + 1]))
        {
            uint8_t count = 0;
            size_t pos = i + 1;

            while (isdigit((unsigned char)nameTemplate[pos]))
            {
                count = (count * 10) + (nameTemplate[pos] - '0');
                pos++;
            }

            char code = nameTemplate[pos];
            if (code == 's')
            {
                appendSuffix(resolved, serialNumber.c_str(), count);
                i = pos;
                continue;
            }
            if (code == 'r')
            {
                appendSuffix(resolved, macHex, count);
                i = pos;
                continue;
            }
        }

        resolved += nameTemplate[i];
    }

    return resolved;
}

static bool isHexDigitString(const char *value)
{
    if (value == nullptr || *value == 0)
    {
        return false;
    }

    while (*value != 0)
    {
        if (!isxdigit((unsigned char)*value))
        {
            return false;
        }
        value++;
    }

    return true;
}

/*
 * Constructor. Nothing at the moment
 */
ELM327Emu::ELM327Emu()
{
    tickCounter = 0;
    ibWritePtr = 0;
    rawWritePtr = 0;
    incomingPrintable[0] = 0;
    ecuAddress = 0x7DF;
    mClient = 0;
    activeTransport = TRANSPORT_BLE;
    bEcho = false;
    bHeader = false;
    bLineFeed = true;
    bMonitorMode = false;
    bDLC = false;
    bSpaces = true;
    bBatchedCommands = false;
    batchOutputFormat = 0;
    sendingBus = 0;
    currentProtocol = 6;

    waitingForReply = false;
    requestStartTime = 0;
    lastReplyTime = 0;

    pendingMode = 0;
    pendingPID = 0;
    pendingVoltageRequest = false;

    gotReply = false;

    activeTxn = 0;

    replyAccumulator = "";
    batchActive = false;
    batchRemainder = "";
    batchAccumulator = "";

    multiFrameActive = false;
    multiFrameExpectedLen = 0;
    multiFrameReceivedLen = 0;
    multiFrameNextSeq = 1;
    multiFrameReplyId = 0;
}

void ELM327Emu::setWiFiClient(WiFiClient *client)
{
    mClient = client;
}

void ELM327Emu::useBLETransport()
{
    activeTransport = TRANSPORT_BLE;
}

void ELM327Emu::useSerialTransport()
{
    activeTransport = TRANSPORT_SERIAL;
}

void ELM327Emu::useClassicBtTransport()
{
    activeTransport = TRANSPORT_CLASSIC_BT;
}

bool ELM327Emu::getMonitorMode()
{
    return bMonitorMode;
}

/*
 * Send a command to ichip. The "AT+i" part will be added.
 */
void ELM327Emu::sendCmd(String cmd)
{
    txBuffer.sendString("AT");
    txBuffer.sendString(cmd);
    txBuffer.sendByteToBuffer(13);

    sendTxBuffer();

    loop(); // parse the response
}

/*
 * Called in the main loop (hopefully) in order to process serial input waiting for us
 * from the wifi module. It should always terminate its answers with 13 so buffer
 * until we get 13 (CR) and then process it.
 * But, for now just echo stuff to our serial port for debugging
 */

void ELM327Emu::loop()
{
    int incoming;

    if (mClient) // wifi and there is a client
    {
        while (mClient->available())
        {
            activeTransport = TRANSPORT_WIFI;
            incoming = mClient->read();

            if (incoming != -1)
            {
                processIncomingByte((uint8_t)incoming);
            }
            else
            {
                return;
            }
        }
    }

    // ===== REQUEST HANDLING =====
    if (waitingForReply)
    {
        uint32_t now = millis();

        // ---- collected replies, wait a short quiet window ----
        if (gotReply)
        {
            if (multiFrameActive && (now - lastReplyTime) > 200)
            {
                multiFrameActive = false;
                pendingVoltageRequest = false;
                completePendingReply("NO DATA\r\n");
            }
            else if (!multiFrameActive && (now - lastReplyTime) > 8)
            {
                flushPendingReply();
            }
        }
        // ---- no reply at all ----
        else
        {
            if ((now - requestStartTime) > 200)
            {
                pendingVoltageRequest = false;
                completePendingReply("NO DATA\r\n");
            }
        }
    }
}

void ELM327Emu::flushPendingReply()
{
    completePendingReply(replyAccumulator);
}

void ELM327Emu::completePendingReply(const String& responseBody)
{
    waitingForReply = false;

    String response = responseBody + ">";
    DEBUG("[ELM %lu %s] APP TX: %s\n",
          activeTxn,
          activeTransportName(),
          escapeElmLogText(response).c_str());

    if (batchActive)
    {
        appendBatchResponse(response);

        if (!processNextBatchCommand())
        {
            sendBatchResponse();
        }
        return;
    }

    txBuffer.sendString(responseBody);
    txBuffer.sendString(">");
    sendTxBuffer();
}

bool ELM327Emu::isFastPollEligible() const
{
    return settings.elmFastPoll &&
           pendingMode == 0x01 &&
           ecuAddress != currentProtocolFunctionalId() &&
           !multiFrameActive;
}

const char *ELM327Emu::activeTransportName() const
{
    switch (activeTransport)
    {
    case TRANSPORT_WIFI:
        return "TCP";
    case TRANSPORT_BLE:
        return "BLE";
    case TRANSPORT_SERIAL:
        return "USB";
    case TRANSPORT_CLASSIC_BT:
        return "BT";
    default:
        return "?";
    }
}

bool ELM327Emu::isCurrentProtocolSupported() const
{
    return currentProtocol == 6 ||
           currentProtocol == 7 ||
           currentProtocol == 8 ||
           currentProtocol == 9;
}

bool ELM327Emu::isCurrentProtocolExtended() const
{
    return currentProtocol == 7 ||
           currentProtocol == 9;
}

uint32_t ELM327Emu::currentProtocolBitrate() const
{
    return (currentProtocol == 8 || currentProtocol == 9) ? 250000 : 500000;
}

uint32_t ELM327Emu::currentProtocolFunctionalId() const
{
    return isCurrentProtocolExtended() ? 0x18DB33F1 : 0x7DF;
}

void ELM327Emu::setProtocol(uint8_t protocol)
{
    currentProtocol = protocol;

    if (!isCurrentProtocolSupported())
    {
        return;
    }

    ecuAddress = currentProtocolFunctionalId();

    if (canBuses[sendingBus] != nullptr)
    {
        canBuses[sendingBus]->begin(currentProtocolBitrate());
        canBuses[sendingBus]->watchFor();
    }
}

void ELM327Emu::sendTxBuffer()
{
    if (activeTransport == TRANSPORT_WIFI &&
        mClient &&
        mClient->connected())
    {
        TransportEndpoint endpoint(mClient);

        txBuffer.flushToEndpoint(endpoint);
    }
    else if (activeTransport == TRANSPORT_SERIAL)
    {
        TransportEndpoint endpoint(&Serial);

        txBuffer.flushToEndpoint(endpoint);
    }
    else if (activeTransport == TRANSPORT_CLASSIC_BT)
    {
        ClassicBtElm327Server *classicBtServer = ClassicBtElm327Server::getInstance();
        if (classicBtServer != nullptr)
        {
            String response((const char *)txBuffer.getBufferedBytes(), txBuffer.numAvailableBytes());
            classicBtServer->sendResponse(response);
        }

        txBuffer.clearBufferedBytes();
    }
    else
    {
        BleElm327Server *bleServer = BleElm327Server::getInstance();
        if (bleServer != nullptr)
        {
            String response((const char *)txBuffer.getBufferedBytes(), txBuffer.numAvailableBytes());
            bleServer->notifyResponse(response);
        }

        txBuffer.clearBufferedBytes();
    }
}

/*
 *   There is no need to pass the string in here because it is local to the class so this function can grab it by default
 *   But, for reference, this cmd processes the command in incomingBuffer
 */
void ELM327Emu::processCmd()
{
    if (incomingBuffer[0] == 0)
    {
        return;
    }

    if (waitingForReply && gotReply && !multiFrameActive)
    {
        flushPendingReply();
    }

    DEBUG("[%s APP->ELM RX] %s\n",
          activeTransportName(),
          incomingPrintable[0] ? incomingPrintable : incomingBuffer);

    if (bBatchedCommands && strchr(incomingBuffer, '|') != nullptr)
    {
        processBatchLine(incomingBuffer);
        return;
    }

    char *cmd = incomingBuffer;

    while (cmd && *cmd)
    {
        char *next = nullptr;

        if (bBatchedCommands)
        {
            next = strchr(cmd, '|');

            if (next)
            {
                *next = 0;
                next++;
            }
        }

        if (*cmd)
        {
            String retString = processELMCmd(cmd);

            if (retString.length() > 0)
            {
                DEBUG("[ELM %s] APP TX: %s\n",
                      activeTransportName(),
                      escapeElmLogText(retString).c_str());
                txBuffer.sendString(retString);
                sendTxBuffer();
            }
        }

        cmd = next;
    }
}

void ELM327Emu::processBatchLine(char* line)
{
    batchActive = true;
    batchAccumulator = "";
    batchRemainder = line;

    if (!processNextBatchCommand())
    {
        sendBatchResponse();
    }
}

bool ELM327Emu::processNextBatchCommand()
{
    while (batchRemainder.length() > 0)
    {
        int separator = batchRemainder.indexOf('|');
        String command;

        if (separator >= 0)
        {
            command = batchRemainder.substring(0, separator);
            batchRemainder = batchRemainder.substring(separator + 1);
        }
        else
        {
            command = batchRemainder;
            batchRemainder = "";
        }

        command.trim();
        if (command.length() == 0)
        {
            continue;
        }

        char cmdBuffer[128];
        command.toCharArray(cmdBuffer, sizeof(cmdBuffer));

        String retString = processELMCmd(cmdBuffer);

        if (retString.length() > 0)
        {
            appendBatchResponse(retString);
            continue;
        }

        if (waitingForReply)
        {
            return true;
        }
    }

    return false;
}

void ELM327Emu::appendBatchResponse(String response)
{
    response.trim();

    if (response.endsWith(">"))
    {
        response.remove(response.length() - 1);
    }

    response.trim();

    if (response.length() == 0)
    {
        return;
    }

    if (batchAccumulator.length() > 0)
    {
        batchAccumulator += "|";
    }

    batchAccumulator += response;
}

void ELM327Emu::sendBatchResponse()
{
    String response = batchAccumulator + ">";

    DEBUG("[ELM %s] APP TX: %s\n",
          activeTransportName(),
          escapeElmLogText(response).c_str());

    txBuffer.sendString(response);
    sendTxBuffer();

    batchActive = false;
    batchRemainder = "";
    batchAccumulator = "";
}

String ELM327Emu::processELMCmd(char *cmd)
{
    String retString = String();

    String lineEnding;
    if (bLineFeed)
        lineEnding = String("\r\n");
    else
        lineEnding = String("\r");

    const bool classicBtIdentity = activeTransport == TRANSPORT_CLASSIC_BT;
    String adapterModel = classicBtIdentity ? String(OPENELM_CLASSIC_MODEL_NAME) : String(OPENELM_MODEL_NAME);
    String adapterName = classicBtIdentity ? buildOpenElmClassicName() : String(settings.btName);

    if (cmd == nullptr || cmd[0] == 0)
    {
        return "";
    }

    bool suppressEcho = false;
    if (!strncmp(cmd, "st", 2))
    {
        suppressEcho =
            !strcmp(cmd, "sti") ||
            !strcmp(cmd, "stix") ||
            !strcmp(cmd, "stdi") ||
            !strcmp(cmd, "stdix") ||
            !strcmp(cmd, "stmfr") ||
            !strcmp(cmd, "stsn") ||
            !strcmp(cmd, "stpr") ||
            !strcmp(cmd, "stprs") ||
            !strcmp(cmd, "stslcs");
    }

    if (bEcho && !suppressEcho)
    {
        retString.concat(cmd);
        retString.concat(lineEnding);
    }

    // ============================================================
    // ST-style compatibility commands used by several OBD applications.
    // ============================================================
    if (!strncmp(cmd, "st", 2))
    {
        if (!strcmp(cmd, "sti"))
        {
            retString.concat(OPENELM_FIRMWARE_REVISION);
        }
        else if (!strcmp(cmd, "stix"))
        {
            retString.concat(OPENELM_FIRMWARE_REVISION);
        }
        else if (!strcmp(cmd, "stdi"))
        {
            retString.concat(adapterModel);
            retString.concat(" r1.0.0");
        }
        else if (!strcmp(cmd, "stmfr"))
        {
            retString.concat(OPENELM_MANUFACTURER);
        }
        else if (!strcmp(cmd, "stsn"))
        {
            retString.concat(buildOpenElmSerialNumber());
        }
        else if (!strcmp(cmd, "stdix"))
        {
            retString.concat("Device: ");
            retString.concat(adapterModel);
            retString.concat(" r1.0.0");
            retString.concat(lineEnding);
            retString.concat("Firmware: ");
            retString.concat(OPENELM_FIRMWARE_REVISION);
            retString.concat(lineEnding);
            retString.concat("Mfr: ");
            retString.concat(OPENELM_MANUFACTURER);
            retString.concat(lineEnding);
            retString.concat("Serial #: ");
            retString.concat(buildOpenElmSerialNumber());
            retString.concat(lineEnding);
            retString.concat("BL Ver: 4.3");
            retString.concat(lineEnding);
            retString.concat("BT Dev Name: ");
            retString.concat(adapterName);
        }
        else if (!strcmp(cmd, "stprs"))
        {
            switch (currentProtocol)
            {
            case 6:
                retString.concat("ISO 15765-4 (CAN 11/500)");
                break;
            case 7:
                retString.concat("ISO 15765-4 (CAN 29/500)");
                break;
            case 8:
                retString.concat("ISO 15765-4 (CAN 11/250)");
                break;
            case 9:
                retString.concat("ISO 15765-4 (CAN 29/250)");
                break;
            default:
                retString.concat("UNKNOWN");
                break;
            }
        }
        else if (!strcmp(cmd, "stpr"))
        {
            sprintf(buffer, "%X", currentProtocol);
            retString.concat(buffer);
        }
        else if (!strncmp(cmd, "stpbr", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stpto", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stptot", 6))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stptrq", 6))
        {
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "stpc"))
        {
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "stpo"))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stbcof", 6))
        {
            uint8_t requested = strtol(cmd + 6, NULL, 10);
            if (requested <= 2)
            {
                batchOutputFormat = requested;
            }

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stbc", 4))
        {
            if (cmd[4] == '1')
            {
                bBatchedCommands = true;
            }
            else if (cmd[4] == '0')
            {
                bBatchedCommands = false;
            }

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stuil", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stbtpm", 6))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stbtdn", 6))
        {
            String newName = resolveBluetoothNameTemplate(cmd + 6);
            if (newName.length() == 0)
            {
                retString.concat("?");
            }
            else
            {
                newName.toCharArray(settings.btName, sizeof(settings.btName));
                newName.toCharArray(deviceName, sizeof(deviceName));

                prefs.begin(PREF_NAME, false);
                prefs.putString("btname", settings.btName);
                prefs.end();

                DEBUG("[ELM] STBTDN set broadcast name: %s\n", settings.btName);
                retString.concat("OK");
            }
        }
        else if (!strncmp(cmd, "stp", 3))
        {
            uint8_t requested = strtol(cmd + 3, NULL, 16);

            if (requested == 0)
            {
                setProtocol(6);
            }
            else if (requested >= 6 && requested <= 9)
            {
                setProtocol(requested);
            }
            else
            {
                currentProtocol = requested;
            }

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stcaf", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stcsegr", 7))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "stcsegt", 7))
        {
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "stslcs"))
        {
            retString.concat("UART_SLEEP:OFF");
            retString.concat(lineEnding);
            retString.concat("OBD_SLEEP:OFF");
            retString.concat(lineEnding);
            retString.concat("VOLT_SLEEP:OFF");
        }
        else if (!strncmp(cmd, "stma", 4))
        {
            bMonitorMode = true;
            retString.concat("OK");
        }
        else
        {
            DEBUG("Unhandled ST command: %s", cmd);
            retString.concat("OK");
        }

        retString.concat(lineEnding);
        retString.concat(">");
        return retString;
    }

    // ============================================================
    // AT command support
    // ============================================================
    if (!strncmp(cmd, "at", 2))
    {
        if (!strcmp(cmd, "atz"))
        {
            bEcho = false;
            bHeader = false;
            bLineFeed = true;
            bMonitorMode = false;
            bDLC = false;
            bSpaces = true;
            bBatchedCommands = false;
            batchOutputFormat = 0;
            setProtocol(6);

            retString.concat("ELM327 v1.4b");
        }
        else if (!strncmp(cmd, "atsh", 4))
        {
            char *idText = cmd + 4;

            if (strlen(idText) == 6 &&
                idText[0] == '0' &&
                idText[1] == '0')
            {
                idText += 2;
            }

            size_t idSize = strlen(idText);

            if (idSize == 3 || idSize == 4 || idSize == 8)
            {
                ecuAddress = Utility::parseHexString(idText, idSize);
                DEBUG("[%s ELM] ECU address: %x\n",
                      activeTransportName(),
                      ecuAddress);
                retString.concat("OK");
            }
            else
            {
                DEBUG("Invalid ATSH header: %s", cmd + 4);
                retString.concat("?");
            }
        }
        else if (!strncmp(cmd, "ate", 3))
        {
            if (cmd[3] == '1')
                bEcho = true;
            if (cmd[3] == '0')
                bEcho = false;

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "ath", 3))
        {
            if (cmd[3] == '1')
                bHeader = true;
            else
                bHeader = false;

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atl", 3))
        {
            if (cmd[3] == '1')
                bLineFeed = true;
            else
                bLineFeed = false;

            retString.concat("OK");
        }
        else if (!strcmp(cmd, "at@1"))
        {
            retString.concat(adapterModel);
        }
        else if (!strcmp(cmd, "at@2"))
        {
            retString.concat(adapterName);
        }
        else if (!strcmp(cmd, "ati"))
        {
            retString.concat(adapterModel);
        }
        else if (!strncmp(cmd, "atat", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "attp", 4))
        {
            ecuAddress = 0x7E0;
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atsp", 4))
        {
            uint8_t requested = strtol(cmd + 4, NULL, 16);

            switch (requested)
            {
            case 0:
                setProtocol(6);
                retString.concat("OK");
                break;

            case 6:
            case 7:
            case 8:
            case 9:
                setProtocol(requested);
                retString.concat("OK");
                break;

            default:
                currentProtocol = requested;
                retString.concat("OK");
                break;
            }
        }
        else if (!strcmp(cmd, "atdp"))
        {
            switch (currentProtocol)
            {
            case 6:
                retString.concat("ISO 15765-4 (CAN 11/500)");
                break;
            case 7:
                retString.concat("ISO 15765-4 (CAN 29/500)");
                break;
            case 8:
                retString.concat("ISO 15765-4 (CAN 11/250)");
                break;
            case 9:
                retString.concat("ISO 15765-4 (CAN 29/250)");
                break;
            default:
                retString.concat("UNKNOWN");
                break;
            }
        }
        else if (!strcmp(cmd, "atdpn"))
        {
            sprintf(buffer, "A%X", currentProtocol);
            retString.concat(buffer);
        }
        else if (!strncmp(cmd, "atd0", 4))
        {
            bDLC = false;
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atd1", 4))
        {
            bDLC = true;
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "atd"))
        {
            bEcho = false;
            bHeader = false;
            bLineFeed = true;
            bMonitorMode = false;
            bDLC = false;
            bSpaces = true;
            bBatchedCommands = false;
            batchOutputFormat = 0;
            setProtocol(6);

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atma", 4))
        {
            DEBUG("ENTERING monitor mode");
            bMonitorMode = true;
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "ats0") ||
                 !strcmp(cmd, "ats1"))
        {
            if (cmd[3] == '0')
                bSpaces = false;
            else
                bSpaces = true;

            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atm", 3))
        {
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "atrv"))
        {
            if (!isCurrentProtocolSupported())
            {
                retString.concat("NO DATA");
            }
            else
            {
                CAN_FRAME outFrame;
                outFrame.id = currentProtocolFunctionalId();
                outFrame.extended = isCurrentProtocolExtended();
                outFrame.length = 8;
                outFrame.rtr = 0;
                outFrame.data.uint8[0] = 0x02;
                outFrame.data.uint8[1] = 0x01;
                outFrame.data.uint8[2] = 0x42;
                outFrame.data.uint8[3] = 0xAA;
                outFrame.data.uint8[4] = 0xAA;
                outFrame.data.uint8[5] = 0xAA;
                outFrame.data.uint8[6] = 0xAA;
                outFrame.data.uint8[7] = 0xAA;

                uint32_t txn = ++elmTxnCounter;
                activeTxn = txn;

                DEBUG("[ELM %lu %s] CAN TX: cmd:%s->0142 id:%03X len:%u data:%s\n",
                      txn,
                      activeTransportName(),
                      cmd,
                      outFrame.id,
                      outFrame.length,
                      formatCanFrameData(outFrame).c_str());

                canManager.sendFrame(canBuses[sendingBus], outFrame);

                gotReply = false;
                replyAccumulator = "";
                multiFrameActive = false;
                multiFrameExpectedLen = 0;
                multiFrameReceivedLen = 0;
                multiFrameNextSeq = 1;
                multiFrameReplyId = 0;

                waitingForReply = true;
                requestStartTime = millis();

                pendingMode = 0x01;
                pendingPID = 0x42;
                pendingVoltageRequest = true;

                return "";
            }
        }
        else if (!strncmp(cmd, "atcaf", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atal", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atcea", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atpc", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atws", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atsw", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atst", 4))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atctm", 5))
        {
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atfi", 4))
        {
            retString.concat("OK");
        }
        else
        {
            DEBUG("Unhandled AT command: %s", cmd);
            retString.concat("OK");
        }

        retString.concat(lineEnding);
        retString.concat(">");
        return retString;
    }

    // ============================================================
    // OBD PID request
    // ============================================================
    size_t cmdSize = strlen(cmd);

    if (cmdSize == 0)
    {
        return "";
    }

    if (!isHexDigitString(cmd) ||
        !(cmdSize == 2 || cmdSize == 3 || cmdSize == 4 ||
          cmdSize == 5 || cmdSize == 6 || cmdSize == 7))
    {
        retString.concat("?");
        retString.concat(lineEnding);
        retString.concat(">");
        return retString;
    }

    CAN_FRAME outFrame;

    if (!isCurrentProtocolSupported())
    {
        retString.concat("NO DATA");
        retString.concat(lineEnding);
        retString.concat(">");
        return retString;
    }

    outFrame.id = ecuAddress;
    outFrame.extended = isCurrentProtocolExtended();
    outFrame.length = 8;
    outFrame.rtr = 0;
    outFrame.data.uint8[3] = 0xAA;
    outFrame.data.uint8[4] = 0xAA;
    outFrame.data.uint8[5] = 0xAA;
    outFrame.data.uint8[6] = 0xAA;
    outFrame.data.uint8[7] = 0xAA;

    bool isStandardPid = false;

    if (cmdSize == 2 || cmdSize == 3)
    {
        isStandardPid = true;

        char tempCmd[3] = {0};
        strncpy(tempCmd, cmd, 2);

        uint32_t valu = strtol(tempCmd, NULL, 16);
        uint8_t mode = (uint8_t)(valu & 0xFF);

        outFrame.data.uint8[0] = 1;
        outFrame.data.uint8[1] = mode;
        outFrame.data.uint8[2] = 0x00;
    }
    else if (cmdSize == 4 || cmdSize == 5)
    {
        isStandardPid = true;

        char tempCmd[5] = {0};
        strncpy(tempCmd, cmd, 4);

        uint32_t valu = strtol(tempCmd, NULL, 16);
        uint8_t pidnum = (uint8_t)(valu & 0xFF);
        uint8_t mode = (uint8_t)((valu >> 8) & 0xFF);

        outFrame.data.uint8[0] = 2;
        outFrame.data.uint8[1] = mode;
        outFrame.data.uint8[2] = pidnum;
    }
    else if (cmdSize == 6 || cmdSize == 7)
    {
        char tempCmd[7] = {0};
        strncpy(tempCmd, cmd, 6);

        uint32_t valu = strtol(tempCmd, NULL, 16);
        uint16_t pidnum = (uint16_t)(valu & 0xFFFF);
        uint8_t mode = (uint8_t)((valu >> 16) & 0xFF);

        outFrame.data.uint8[0] = 3;
        outFrame.data.uint8[1] = mode;
        outFrame.data.uint8[2] = pidnum >> 8;
        outFrame.data.uint8[3] = pidnum & 0xFF;
    }

    uint32_t txn = ++elmTxnCounter;

    activeTxn = txn;

    DEBUG("[ELM %lu %s] CAN TX: cmd:%s id:%03X len:%u data:%s\n",
          txn,
          activeTransportName(),
          cmd,
          outFrame.id,
          outFrame.length,
          formatCanFrameData(outFrame).c_str());

    canManager.sendFrame(canBuses[sendingBus], outFrame);

    gotReply = false;
    replyAccumulator = "";
    pendingVoltageRequest = false;
    multiFrameActive = false;
    multiFrameExpectedLen = 0;
    multiFrameReceivedLen = 0;
    multiFrameNextSeq = 1;
    multiFrameReplyId = 0;

    waitingForReply = true;
    requestStartTime = millis();

    pendingMode = outFrame.data.uint8[1];

    if (isStandardPid)
    {
        pendingPID = outFrame.data.uint8[2];
    }
    else
    {
        pendingPID =
            ((uint16_t)outFrame.data.uint8[2] << 8) |
            outFrame.data.uint8[3];
    }

    return "";
}

void ELM327Emu::processCANReply(CAN_FRAME &frame)
{
    // ignore unrelated traffic
    if (!waitingForReply)
    {
        return;
    }

    // basic OBD/UDS positive response filtering
    if (frame.length < 2)
    {
        return;
    }

    uint8_t pciType = frame.data.uint8[0] & 0xF0;
    uint8_t sid = frame.data.uint8[1];

    if (pciType == 0x10 && frame.length >= 3)
    {
        sid = frame.data.uint8[2];
    }
    else if (pciType == 0x20)
    {
        if (!multiFrameActive || frame.id != multiFrameReplyId)
        {
            return;
        }

        sid = pendingMode + 0x40;
    }

    // positive response SID must equal request SID + 0x40
    bool positive =
        (sid == (pendingMode + 0x40));

    bool negative =
        (sid == 0x7F);

    if (!positive && !negative)
    {
        return;
    }

    lastReplyTime = millis();

    gotReply = true;

    DEBUG("[ELM %lu %s] CAN RX: id:%03X len:%u data:%s\n",
          activeTxn,
          activeTransportName(),
          frame.id,
          frame.length,
          formatCanFrameData(frame).c_str());

    if (pendingVoltageRequest)
    {
        pendingVoltageRequest = false;

        uint8_t payloadLen = frame.data.uint8[0];
        const uint8_t *payload = &frame.data.uint8[1];

        if (positive &&
            pciType == 0x00 &&
            payloadLen >= 4 &&
            payload[0] == 0x41 &&
            payload[1] == 0x42)
        {
            uint16_t millivolts =
                ((uint16_t)payload[2] << 8) |
                payload[3];
            uint16_t decivolts = (millivolts + 50) / 100;

            char voltageText[16];
            sprintf(voltageText,
                    "%u.%uV",
                    decivolts / 10,
                    decivolts % 10);

            replyAccumulator = voltageText;
        }
        else
        {
            replyAccumulator = "NO DATA";
        }

        if (bLineFeed)
        {
            replyAccumulator += "\r\n";
        }
        else
        {
            replyAccumulator += "\r";
        }

        return;
    }

    auto appendRawIsoTpLine = [&](const CAN_FRAME &replyFrame,
                                  uint8_t byteCount) {
        char buff[32];

        if (bHeader || bMonitorMode)
        {
            if (bSpaces)
            {
                sprintf(buff, "%03X ", replyFrame.id);
            }
            else
            {
                sprintf(buff, "%03X", replyFrame.id);
            }

            replyAccumulator += buff;
        }

        for (uint8_t i = 0; i < byteCount && i < replyFrame.length; i++)
        {
            if (bSpaces)
            {
                sprintf(buff, "%02X ", replyFrame.data.uint8[i]);
            }
            else
            {
                sprintf(buff, "%02X", replyFrame.data.uint8[i]);
            }

            replyAccumulator += buff;
        }

        if (bSpaces &&
            replyAccumulator.length() > 0 &&
            replyAccumulator.endsWith(" "))
        {
            replyAccumulator.remove(replyAccumulator.length() - 1);
        }

        if (bLineFeed)
        {
            replyAccumulator += "\r\n";
        }
        else
        {
            replyAccumulator += "\r";
        }
    };

    if (pciType == 0x10)
    {
        uint16_t totalLen =
            ((uint16_t)(frame.data.uint8[0] & 0x0F) << 8) |
            frame.data.uint8[1];

        if (totalLen == 0 || totalLen > sizeof(multiFramePayload))
        {
            return;
        }

        multiFrameActive = true;
        multiFrameExpectedLen = totalLen;
        multiFrameReceivedLen = 0;
        multiFrameNextSeq = 1;
        multiFrameReplyId = frame.id;

        uint8_t firstPayloadLen = totalLen < 6 ? totalLen : 6;
        for (uint8_t i = 0; i < firstPayloadLen; i++)
        {
            multiFramePayload[multiFrameReceivedLen++] = frame.data.uint8[2 + i];
        }

        CAN_FRAME flowControl;
        flowControl.id = ecuAddress;
        if (flowControl.id == 0x7DF && frame.id >= 0x7E8 && frame.id <= 0x7EF)
        {
            flowControl.id = frame.id - 8;
        }
        else if (isCurrentProtocolExtended() &&
                 frame.extended &&
                 (frame.id & 0x1FFF0000) == 0x18DA0000)
        {
            uint8_t source = frame.id & 0xFF;
            uint8_t target = (frame.id >> 8) & 0xFF;
            flowControl.id = 0x18DA0000 | ((uint32_t)source << 8) | target;
        }
        flowControl.extended = isCurrentProtocolExtended();
        flowControl.length = 8;
        flowControl.rtr = 0;
        flowControl.data.uint8[0] = 0x30;
        flowControl.data.uint8[1] = 0x00;
        flowControl.data.uint8[2] = 0x00;
        flowControl.data.uint8[3] = 0xAA;
        flowControl.data.uint8[4] = 0xAA;
        flowControl.data.uint8[5] = 0xAA;
        flowControl.data.uint8[6] = 0xAA;
        flowControl.data.uint8[7] = 0xAA;
        canManager.sendFrame(canBuses[sendingBus], flowControl);

        lastReplyTime = millis();
        gotReply = true;

        if (bHeader || bMonitorMode)
        {
            appendRawIsoTpLine(frame, 2 + firstPayloadLen);
        }

        return;
    }

    if (pciType == 0x20)
    {
        if (!multiFrameActive || frame.id != multiFrameReplyId)
        {
            return;
        }

        uint8_t seq = frame.data.uint8[0] & 0x0F;
        if (seq != multiFrameNextSeq)
        {
            multiFrameActive = false;
            return;
        }

        multiFrameNextSeq = (multiFrameNextSeq + 1) & 0x0F;

        uint8_t copiedBytes = 0;
        for (uint8_t i = 1;
             i < frame.length && multiFrameReceivedLen < multiFrameExpectedLen;
             i++)
        {
            multiFramePayload[multiFrameReceivedLen++] = frame.data.uint8[i];
            copiedBytes++;
        }

        lastReplyTime = millis();
        gotReply = true;

        if (bHeader || bMonitorMode)
        {
            appendRawIsoTpLine(frame, 1 + copiedBytes);
        }

        if (multiFrameReceivedLen < multiFrameExpectedLen)
        {
            return;
        }

        multiFrameActive = false;

        if (bHeader || bMonitorMode)
        {
            return;
        }

        frame.id = multiFrameReplyId;
        frame.length = 8;
        frame.data.uint8[0] = multiFrameExpectedLen;
        for (uint8_t i = 0; i < 7; i++)
        {
            frame.data.uint8[1 + i] =
                i < multiFrameExpectedLen ? multiFramePayload[i] : 0;
        }
    }

    char buff[32];

    // ===== HEADER =====
    if (bHeader || bMonitorMode)
    {
        if (bSpaces)
        {
            sprintf(buff,
                    "%03X ",
                    frame.id);
        }
        else
        {
            sprintf(buff,
                    "%03X",
                    frame.id);
        }

        replyAccumulator += buff;
    }

    // ===== DLC / PCI =====
    if (bDLC || bHeader)
    {
        if (bSpaces)
        {
            sprintf(buff,
                    "%02X ",
                    frame.data.uint8[0]);
        }
        else
        {
            sprintf(buff,
                    "%02X",
                    frame.data.uint8[0]);
        }

        replyAccumulator += buff;
    }

    // ===== PAYLOAD =====
    // byte0 = ISO-TP payload length
    // payload starts at byte1

    uint8_t payloadLen = frame.data.uint8[0];
    const uint8_t *payload = &frame.data.uint8[1];

    if (payloadLen > 7)
    {
        payloadLen = multiFrameExpectedLen;
        payload = multiFramePayload;
    }

    for (int i = 0; i < payloadLen; i++)
    {
        if (bSpaces)
        {
            sprintf(buff,
                    "%02X ",
                    payload[i]);
        }
        else
        {
            sprintf(buff,
                    "%02X",
                    payload[i]);
        }

        replyAccumulator += buff;
    }

    // trim trailing space only when spaces enabled
    if (bSpaces &&
        replyAccumulator.length() > 0 &&
        replyAccumulator.endsWith(" "))
    {
        replyAccumulator.remove(
            replyAccumulator.length() - 1);
    }

    if (bLineFeed)
    {
        replyAccumulator += "\r\n";
    }
    else
    {
        replyAccumulator += "\r";
    }

    if (isFastPollEligible())
    {
        flushPendingReply();
    }
}

void ELM327Emu::processIncomingByte(uint8_t incoming)
{
    captureIncomingPrintable(incoming);

    if (incoming == 13 || incoming == 10 || ibWritePtr > 126)
    {
        if (ibWritePtr == 0)
        {
            rawWritePtr = 0;
            incomingPrintable[0] = 0;
            return;
        }

        incomingBuffer[ibWritePtr] = 0;
        ibWritePtr = 0;
        rawWritePtr = 0;

        processCmd();
        incomingPrintable[0] = 0;
        return;
    }

    if (incoming > 20 && bMonitorMode)
    {
        DEBUG("Exiting monitor mode");
        bMonitorMode = false;
    }

    if (incoming == ' ' || incoming == '\t')
    {
        return;
    }

    incomingBuffer[ibWritePtr++] = (char)tolower((unsigned char)incoming);
}

void ELM327Emu::captureIncomingPrintable(uint8_t incoming)
{
    if (rawWritePtr >= (int)(sizeof(incomingPrintable) - 5))
    {
        return;
    }

    if (incoming == '\r')
    {
        incomingPrintable[rawWritePtr++] = '\\';
        incomingPrintable[rawWritePtr++] = 'r';
    }
    else if (incoming == '\n')
    {
        incomingPrintable[rawWritePtr++] = '\\';
        incomingPrintable[rawWritePtr++] = 'n';
    }
    else if (incoming == '\t')
    {
        incomingPrintable[rawWritePtr++] = '\\';
        incomingPrintable[rawWritePtr++] = 't';
    }
    else if (incoming >= 0x20 && incoming <= 0x7E)
    {
        incomingPrintable[rawWritePtr++] = (char)incoming;
    }
    else
    {
        sprintf(&incomingPrintable[rawWritePtr], "\\x%02X", incoming);
        rawWritePtr += 4;
    }

    incomingPrintable[rawWritePtr] = 0;
}
