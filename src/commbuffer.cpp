#include "commbuffer.h"
#include "Logger.h"
#include "gvret_comm.h"
#include <stdarg.h>
#include <stdio.h>

CommBuffer::CommBuffer()
{
    transmitBufferLength = 0;
}

size_t CommBuffer::numAvailableBytes()
{
    return transmitBufferLength;
}

void CommBuffer::clearBufferedBytes()
{
    transmitBufferLength = 0;
}

uint8_t *CommBuffer::getBufferedBytes()
{
    return transmitBuffer;
}

// a bit faster version that blasts through the copy more efficiently.
void CommBuffer::sendBytesToBuffer(uint8_t *bytes, size_t length)
{
    size_t remaining =
        WIFI_BUFF_SIZE - transmitBufferLength;

    if (length > remaining)
    {
        length = remaining;
    }

    if (length == 0)
    {
        return;
    }

    memcpy(&transmitBuffer[transmitBufferLength],
           bytes,
           length);

    transmitBufferLength += length;
}

void CommBuffer::sendByteToBuffer(uint8_t byt)
{
    if (transmitBufferLength >= WIFI_BUFF_SIZE)
    {
        return;
    }

    transmitBuffer[transmitBufferLength++] = byt;
}

void CommBuffer::sendString(String str)
{
    char buff[300];
    str.toCharArray(buff, 300);
    sendCharString(buff);
}

void CommBuffer::sendCharString(char *str)
{
    char *p = str;
    int i = 0;
    while (*p)
    {
        sendByteToBuffer(*p++);
        i++;
    }
    Logger::debug("Queued %i bytes", i);
}

bool CommBuffer::appendFormatted(const char *fmt, ...)
{
    if (transmitBufferLength >= WIFI_BUFF_SIZE)
    {
        return false;
    }

    va_list args;

    va_start(args, fmt);

    int written = vsnprintf(
        (char *)&transmitBuffer[transmitBufferLength],
        WIFI_BUFF_SIZE - transmitBufferLength,
        fmt,
        args);

    va_end(args);

    if (written < 0)
    {
        return false;
    }

    if (written >= (WIFI_BUFF_SIZE - transmitBufferLength))
    {
        overflowEvents++;
        
        transmitBufferLength = WIFI_BUFF_SIZE;
        return false;
    }

    transmitBufferLength += written;

    return true;
}

void CommBuffer::sendFrameToBuffer(CAN_FRAME &frame, int whichBus)
{
    uint8_t temp;
    size_t writtenBytes;
    if (settings.useBinarySerialComm)
    {
        size_t needed = 12 + frame.length;

        if (!hasSpace(needed))
        {
            droppedFrames++;
            overflowEvents++;

            return;
        }

        if (frame.extended)
            frame.id |= 1 << 31;

        transmitBuffer[transmitBufferLength++] = 0xF1;
        transmitBuffer[transmitBufferLength++] = 0; // 0 = canbus frame sending
        uint32_t now = micros();
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 24);
        transmitBuffer[transmitBufferLength++] = frame.length + (uint8_t)(whichBus << 4);
        for (int c = 0; c < frame.length; c++)
        {
            transmitBuffer[transmitBufferLength++] = frame.data.uint8[c];
        }
        // temp = checksumCalc(buff, 11 + frame.length);
        temp = 0;
        transmitBuffer[transmitBufferLength++] = temp;
        // Serial.write(buff, 12 + frame.length);
    }
    else
    {
        appendFormatted("%d - %x", micros(), frame.id);
        appendFormatted(frame.extended ? " X " : " S ");
        appendFormatted("%i %i", whichBus, frame.length);
        for (int c = 0; c < frame.length; c++)
        {
            appendFormatted(" %x", frame.data.uint8[c]);
        }
        appendFormatted("\r\n");
    }
}

void CommBuffer::sendFrameToBuffer(CAN_FRAME_FD &frame, int whichBus)
{
    uint8_t temp;
    size_t writtenBytes;
    if (settings.useBinarySerialComm)
    {
        size_t needed = 13 + frame.length;

        if (!hasSpace(needed))
        {
            droppedFrames++;
            overflowEvents++;
            
            return;
        }
        if (frame.extended)
            frame.id |= 1 << 31;

        transmitBuffer[transmitBufferLength++] = 0xF1;
        transmitBuffer[transmitBufferLength++] = PROTO_BUILD_FD_FRAME;
        uint32_t now = micros();
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 24);
        transmitBuffer[transmitBufferLength++] = frame.length;
        transmitBuffer[transmitBufferLength++] = (uint8_t)(whichBus);
        for (int c = 0; c < frame.length; c++)
        {
            transmitBuffer[transmitBufferLength++] = frame.data.uint8[c];
        }
        // temp = checksumCalc(buff, 11 + frame.length);
        temp = 0;
        transmitBuffer[transmitBufferLength++] = temp;
        // Serial.write(buff, 12 + frame.length);
    }
    else
    {
        appendFormatted("%d - %x", micros(), frame.id);
        appendFormatted(frame.extended ? " X " : " S ");
        appendFormatted("%i %i", whichBus, frame.length);
        for (int c = 0; c < frame.length; c++)
        {
            appendFormatted(" %x", frame.data.uint8[c]);
        }
        appendFormatted("\r\n");
    }
}

void CommBuffer::consume(size_t n)
{
    if (n == 0)
        return;

    if (n >= transmitBufferLength)
    {
        transmitBufferLength = 0;
        return;
    }

    // shift remaining bytes to front
    memmove(transmitBuffer, transmitBuffer + n, transmitBufferLength - n);
    transmitBufferLength -= n;
}

size_t CommBuffer::flushToEndpoint(TransportEndpoint &endpoint)
{
    size_t available = transmitBufferLength;

    if (available == 0)
    {
        return 0;
    }

    if (!endpoint.connected())
    {
        return 0;
    }

    size_t sent = endpoint.write(transmitBuffer, available);

    if (sent > 0)
    {
        consume(sent);
    }

    return sent;
}

bool CommBuffer::hasSpace(size_t needed)
{
    return (transmitBufferLength + needed) <= WIFI_BUFF_SIZE;
}

uint32_t CommBuffer::getDroppedFrames()
{
    return droppedFrames;
}

uint32_t CommBuffer::getOverflowEvents()
{
    return overflowEvents;
}