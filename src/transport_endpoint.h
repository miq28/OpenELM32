#pragma once

#include <Arduino.h>
#include <Client.h>
#include <Stream.h>

class TransportEndpoint
{
public:
    TransportEndpoint()
        : client(nullptr),
          stream(nullptr)
    {
    }

    TransportEndpoint(Client *c)
        : client(c),
          stream(nullptr)
    {
    }

    TransportEndpoint(Stream *s)
        : client(nullptr),
          stream(s)
    {
    }

    size_t write(const uint8_t *buf, size_t len)
    {
        if (client)
        {
            return client->write(buf, len);
        }

        if (stream)
        {
            return stream->write(buf, len);
        }

        return 0;
    }

    bool connected() const
    {
        if (client)
        {
            return client->connected();
        }

        // Stream usually always "connected"
        if (stream)
        {
            return true;
        }

        return false;
    }

private:
    Client *client;
    Stream *stream;
};