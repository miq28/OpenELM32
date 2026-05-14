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
#ifndef CONFIG_IDF_TARGET_ESP32S3
#include "BluetoothSerial.h"
#endif

/*
 * Constructor. Nothing at the moment
 */
ELM327Emu::ELM327Emu()
{
    tickCounter = 0;
    ibWritePtr = 0;
    ecuAddress = 0x7E0;
    mClient = 0;
    bEcho = false;
    bHeader = false;
    bLineFeed = true;
    bMonitorMode = false;
    bDLC = false;
    sendingBus = 0;

    waitingForReply = false;
    requestStartTime = 0;
    lastReplyTime = 0;

    pendingMode = 0;
    pendingPID = 0;

    gotReply = false;

    replyAccumulator = "";
}

/*
 * Initialization of hardware and parameters
 */
void ELM327Emu::setup()
{
#ifndef CONFIG_IDF_TARGET_ESP32S3
    serialBT.begin(settings.btName);
#endif
}

void ELM327Emu::setWiFiClient(WiFiClient *client)
{
    mClient = client;
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
    if (!mClient) // bluetooth
    {
#ifndef CONFIG_IDF_TARGET_ESP32S3
        while (serialBT.available())
        {
            incoming = serialBT.read();
            if (incoming != -1)
            { // and there is no reason it should be -1
                if (incoming == 13 || ibWritePtr > 126)
                {                                   // on CR or full buffer, process the line
                    incomingBuffer[ibWritePtr] = 0; // null terminate the string
                    ibWritePtr = 0;                 // reset the write pointer

                    if (Logger::isDebug())
                        Logger::debug(incomingBuffer);

                    processCmd();
                }
                else
                { // add more characters
                    if (incoming > 20 && bMonitorMode)
                    {
                        Logger::debug("Exiting monitor mode");
                        bMonitorMode = false;
                    }
                    if (incoming != 10 && incoming != ' ')                      // don't add a LF character or spaces. Strip them right out
                        incomingBuffer[ibWritePtr++] = (char)tolower(incoming); // force lowercase to make processing easier
                }
            }
            else
                return;
        }
#endif
    }
    else // wifi and there is a client
    {
        while (mClient->available())
        {
            incoming = mClient->read();
            if (incoming != -1)
            { // and there is no reason it should be -1
                if (incoming == 13 || ibWritePtr > 126)
                {                                   // on CR or full buffer, process the line
                    incomingBuffer[ibWritePtr] = 0; // null terminate the string
                    ibWritePtr = 0;                 // reset the write pointer

                    if (Logger::isDebug())
                        Logger::debug(incomingBuffer);

                    processCmd();
                }
                else
                {                                                               // add more characters
                    if (incoming != 10 && incoming != ' ')                      // don't add a LF character or spaces. Strip them right out
                        incomingBuffer[ibWritePtr++] = (char)tolower(incoming); // force lowercase to make processing easier
                }
            }
            else
                return;
        }
    }

    // ===== REQUEST HANDLING =====
    if (waitingForReply)
    {
        uint32_t now = millis();

        // ---- collected replies, wait a short quiet window ----
        if (gotReply)
        {
            if ((now - lastReplyTime) > 50)
            {
                waitingForReply = false;

                txBuffer.sendString(replyAccumulator);

                txBuffer.sendString(">");

                sendTxBuffer();
            }
        }
        // ---- no reply at all ----
        else
        {
            if ((now - requestStartTime) > 200)
            {
                waitingForReply = false;

                txBuffer.sendString("NO DATA\r\n>");

                sendTxBuffer();
            }
        }
    }
}

void ELM327Emu::sendTxBuffer()
{
    if (mClient)
    {
        if (mClient->connected())
        {
            TransportEndpoint endpoint(mClient);

            txBuffer.flushToEndpoint(endpoint);
        }
    }
    else // bluetooth then
    {
#ifndef CONFIG_IDF_TARGET_ESP32S3
        TransportEndpoint endpoint(&serialBT);

        txBuffer.flushToEndpoint(endpoint);
#endif
    }
}

/*
 *   There is no need to pass the string in here because it is local to the class so this function can grab it by default
 *   But, for reference, this cmd processes the command in incomingBuffer
 */
void ELM327Emu::processCmd()
{
    String retString = processELMCmd(incomingBuffer);

    txBuffer.sendString(retString);
    sendTxBuffer();
    if (Logger::isDebug())
    {
        char buff[300];
        retString = "Reply:" + retString;
        retString.toCharArray(buff, 300);
        Logger::debug(buff);
    }
}

String ELM327Emu::processELMCmd(char *cmd)
{
    String retString = String();
    String lineEnding;
    if (bLineFeed)
        lineEnding = String("\r\n");
    else
        lineEnding = String("\r");

    if (bEcho)
    {
        retString.concat(cmd);
        retString.concat(lineEnding);
    }

    if (!strncmp(cmd, "at", 2))
    {

        if (!strcmp(cmd, "atz"))
        { // reset hardware
            retString.concat(lineEnding);
            retString.concat("ELM327 v1.3a");
        }
        else if (!strncmp(cmd, "atsh", 4)) // set header address (address we send queries to)
        {
            size_t idSize = strlen(cmd + 4);
            ecuAddress = Utility::parseHexString(cmd + 4, idSize);
            Logger::debug("New ECU address: %x", ecuAddress);
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "ate", 3))
        { // turn echo on/off
            if (cmd[3] == '1')
                bEcho = true;
            if (cmd[3] == '0')
                bEcho = false;
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "ath", 3))
        { // turn headers on/off
            if (cmd[3] == '1')
                bHeader = true;
            else
                bHeader = false;
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atl", 3))
        { // turn linefeeds on/off
            if (cmd[3] == '1')
                bLineFeed = true;
            else
                bLineFeed = false;
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "at@1"))
        { // send device description
            retString.concat("OBDLink MX");
        }
        else if (!strcmp(cmd, "at@2"))
        { // device identifier
            retString.concat(deviceName); // WEACT_CAN4854
        }
        else if (!strcmp(cmd, "ati"))
        { // send chip ID
            retString.concat("ELM327 v1.5");
        }
        else if (!strncmp(cmd, "atat", 4))
        { // set adaptive timing
            // don't intend to support adaptive timing at all
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atsp", 4))
        { // set protocol
            // theoretically we can ignore this
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "atdp"))
        { // show description of protocol
            retString.concat("can11/500");
        }
        else if (!strcmp(cmd, "atdpn"))
        { // show protocol number (same as passed to sp)
            retString.concat("6");
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
        { // set to defaults
            retString.concat("OK");
        }
        else if (!strncmp(cmd, "atma", 4)) // monitor all mode
        {
            Logger::debug("ENTERING monitor mode");
            bMonitorMode = true;
        }
        else if (!strncmp(cmd, "atm", 3))
        { // turn memory on/off
            retString.concat("OK");
        }
        else if (!strcmp(cmd, "atrv"))
        { // show 12v rail voltage
            // TODO: the system should actually have this value so it wouldn't hurt to
            // look it up and report the real value.
            retString.concat("14.2V");
        }
        //==========
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
        //===================
        else
        { // by default respond to anything not specifically handled by just saying OK and pretending.
            Logger::debug("Unhandled AT command: %s", cmd);
            retString.concat("OK");
        }
    }
    else
    { // if no AT then assume it is a PID request. This takes the form of four bytes which form the alpha hex digit encoding for two bytes
        // there should be four or six characters here forming the ascii representation of the PID request. Easiest for now is to turn the ascii into
        // a 16 bit number and mask off to get the bytes
        CAN_FRAME outFrame;
        outFrame.id = ecuAddress;
        outFrame.extended = false;
        outFrame.length = 8;
        outFrame.rtr = 0;
        outFrame.data.uint8[3] = 0xAA;
        outFrame.data.uint8[4] = 0xAA;
        outFrame.data.uint8[5] = 0xAA;
        outFrame.data.uint8[6] = 0xAA;
        outFrame.data.uint8[7] = 0xAA;
        size_t cmdSize = strlen(cmd);
        if (cmdSize == 4) // generic OBDII codes
        {
            uint32_t valu = strtol((char *)cmd, NULL, 16); // the pid format is always in hex
            uint8_t pidnum = (uint8_t)(valu & 0xFF);
            uint8_t mode = (uint8_t)((valu >> 8) & 0xFF);
            Logger::debug("Mode: %i, PID: %i", mode, pidnum);
            outFrame.data.uint8[0] = 2;
            outFrame.data.uint8[1] = mode;
            outFrame.data.uint8[2] = pidnum;
        }
        if (cmdSize == 6) // custom PIDs for specific vehicles
        {
            uint32_t valu = strtol((char *)cmd, NULL, 16); // the pid format is always in hex
            uint16_t pidnum = (uint8_t)(valu & 0xFFFF);
            uint8_t mode = (uint8_t)((valu >> 16) & 0xFF);
            Logger::debug("Mode: %i, PID: %i", mode, pidnum);
            outFrame.data.uint8[0] = 3;
            outFrame.data.uint8[1] = mode;
            outFrame.data.uint8[2] = pidnum >> 8;
            outFrame.data.uint8[3] = pidnum & 0xFF;
        }
        /* //only for debugging!
        canManager.setSendToConsole(true);
        canManager.displayFrame(outFrame, sendingBus);
        canManager.setSendToConsole(false);
        */
        if (processVirtualOBD(retString, cmd))
        {
            retString.concat(lineEnding);
            retString.concat(">");

            return retString;
        }

        canManager.sendFrame(canBuses[sendingBus], outFrame);

        waitingForReply = true;
        requestStartTime = millis();

        pendingMode = outFrame.data.uint8[1];

        if (cmdSize == 4)
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

    retString.concat(lineEnding);
    retString.concat(">"); // prompt to show we're ready to receive again

    return retString;
}

bool ELM327Emu::isVirtualPIDSupported(uint8_t mode, uint16_t pid)
{
    if (mode != 1)
    {
        return false;
    }

    switch (pid)
    {
    case 0x00:
    case 0x05:
    case 0x0C:
    case 0x0D:
    case 0x0F:
    case 0x11:
    case 0x2F:
    case 0x42:
        return true;

    default:
        return false;
    }
}

uint32_t ELM327Emu::buildPIDBitmap(uint8_t basePID)
{
    uint32_t bitmap = 0;

    for (int i = 1; i <= 32; i++)
    {
        uint16_t pid = basePID + i;

        if (isVirtualPIDSupported(1, pid))
        {
            bitmap |= (1UL << (32 - i));
        }
    }

    return bitmap;
}

bool ELM327Emu::processVirtualOBD(String &retString, char *cmd)
{
    // ===== SUPPORTED PID MAP =====
    if (!strncmp(cmd, "0100", 4))
    {
        uint32_t bitmap = buildPIDBitmap(0x00);

        char temp[64];

        sprintf(temp,
                "41 00 %02X %02X %02X %02X",
                (bitmap >> 24) & 0xFF,
                (bitmap >> 16) & 0xFF,
                (bitmap >> 8) & 0xFF,
                bitmap & 0xFF);

        retString.concat(temp);

        return true;
    }

    // ===== RPM =====
    if (!strncmp(cmd, "010c", 4))
    {
        static uint16_t fakeRPM = 850;

        fakeRPM += 25;

        if (fakeRPM > 3200)
        {
            fakeRPM = 850;
        }

        uint16_t raw = fakeRPM * 4;

        char temp[64];

        sprintf(temp,
                "41 0C %02X %02X",
                (raw >> 8) & 0xFF,
                raw & 0xFF);

        retString.concat(temp);

        return true;
    }

    // ===== VEHICLE SPEED =====
    if (!strncmp(cmd, "010d", 4))
    {
        static uint8_t fakeSpeed = 0;

        fakeSpeed++;

        if (fakeSpeed > 120)
        {
            fakeSpeed = 0;
        }

        char temp[64];

        sprintf(temp,
                "41 0D %02X",
                fakeSpeed);

        retString.concat(temp);

        return true;
    }

    // ===== COOLANT TEMP =====
    if (!strncmp(cmd, "0105", 4))
    {
        uint8_t tempValue = 90 + 40;

        char temp[64];

        sprintf(temp,
                "41 05 %02X",
                tempValue);

        retString.concat(temp);

        return true;
    }

    // ===== BATTERY VOLTAGE =====
    if (!strncmp(cmd, "0142", 4))
    {
        uint16_t voltage = 1420;

        char temp[64];

        sprintf(temp,
                "41 42 %02X %02X",
                (voltage >> 8) & 0xFF,
                voltage & 0xFF);

        retString.concat(temp);

        return true;
    }

    // ===== VIN =====
    if (!strncmp(cmd, "0902", 4))
    {
        retString.concat("49 02 01 57 45 41 43 54 31");
        return true;
    }

    // ===== INTAKE AIR TEMP =====
    if (!strncmp(cmd, "010f", 4))
    {
        uint8_t tempValue = 35 + 40;

        char temp[64];

        sprintf(temp,
                "41 0F %02X",
                tempValue);

        retString.concat(temp);

        return true;
    }

    // ===== THROTTLE POSITION =====
    if (!strncmp(cmd, "0111", 4))
    {
        static uint8_t throttle = 10;

        throttle += 3;

        if (throttle > 90)
        {
            throttle = 10;
        }

        uint8_t raw =
            (uint8_t)((throttle * 255) / 100);

        char temp[64];

        sprintf(temp,
                "41 11 %02X",
                raw);

        retString.concat(temp);

        return true;
    }

    // ===== FUEL LEVEL =====
    if (!strncmp(cmd, "012f", 4))
    {
        uint8_t fuel = 72;

        uint8_t raw =
            (uint8_t)((fuel * 255) / 100);

        char temp[64];

        sprintf(temp,
                "41 2F %02X",
                raw);

        retString.concat(temp);

        return true;
    }

    return false;
}

void ELM327Emu::processCANReply(CAN_FRAME &frame)
{
    lastReplyTime = millis();

    gotReply = true;

    char buff[32];

    // ===== HEADER =====
    if (bHeader || bMonitorMode)
    {
        sprintf(buff, "%03X ", frame.id);

        replyAccumulator += buff;
    }

    // ===== DLC =====
    if (bDLC)
    {
        sprintf(buff, "%u ", frame.length);

        replyAccumulator += buff;
    }

    // ===== DATA =====
    for (int i = 0; i < frame.data.uint8[0]; i++)
    {
        sprintf(buff, "%02X ", frame.data.uint8[1 + i]);

        replyAccumulator += buff;
    }

    // trim trailing space
    if (replyAccumulator.length() > 0 &&
        replyAccumulator.endsWith(" "))
    {
        replyAccumulator.remove(
            replyAccumulator.length() - 1);
    }

    replyAccumulator += "\r\n";
}
