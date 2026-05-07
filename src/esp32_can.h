#pragma once

#include "driver/twai.h"
#include <Arduino.h>
#include <cstring>

typedef struct
{
    uint32_t timestamp;
    uint32_t alerts;
    uint16_t rx_err;
    uint16_t tx_err;
    uint8_t state;
} CAN_EVENT;

class ESP32CAN;

extern ESP32CAN CAN0;
extern ESP32CAN CAN1;

// ================= FRAME TYPES =================
typedef union
{
    uint64_t uint64;
    uint32_t uint32[2];
    uint16_t uint16[4];
    uint8_t uint8[8];
} BytesUnion;

typedef union
{
    uint8_t uint8[64];
} BytesUnionFD;

// add near top or before class
void alertsToText(uint32_t alerts, char *out, size_t len);
const char *stateToStr(uint8_t s);

class CAN_FRAME
{
public:
    CAN_FRAME() : id(0), fid(0), timestamp(0), rtr(0), priority(0), extended(0), length(0) {}
    BytesUnion data;
    uint32_t id;
    uint32_t fid;
    uint32_t timestamp;
    uint8_t rtr;
    uint8_t priority;
    uint8_t extended;
    uint8_t length;
};

class CAN_FRAME_FD
{
public:
    BytesUnionFD data;
    uint32_t id;
    uint32_t fid;
    uint32_t timestamp;
    uint8_t rrs;
    uint8_t priority;
    uint8_t extended;
    uint8_t fdMode;
    uint8_t length;
};

// ================= BASE CLASS =================
class CAN_COMMON
{
public:
    virtual ~CAN_COMMON() = default;

    virtual uint32_t begin(uint32_t baudrate) = 0;

    virtual uint32_t begin(uint32_t baudrate, uint8_t)
    {
        return begin(baudrate);
    }

    virtual uint32_t beginFD(uint32_t, uint32_t)
    {
        return 0;
    }

    virtual void enable() = 0;
    virtual void disable() = 0;

    virtual bool sendFrame(CAN_FRAME &) = 0;

    virtual bool sendFrameFD(CAN_FRAME_FD &)
    {
        return false;
    }

    virtual uint16_t available() = 0;

    virtual uint32_t read(CAN_FRAME &) = 0;

    virtual uint32_t readFD(CAN_FRAME_FD &)
    {
        return 0;
    }

    virtual void watchFor() {}

    virtual void setListenOnlyMode(bool) {}

    virtual bool supportsFDMode()
    {
        return false;
    }

    virtual void setDebuggingMode(bool) {}
};

// ================= ESP32CAN CLASS =================
class ESP32CAN : public CAN_COMMON
{
public:
    using CAN_COMMON::begin;

    ESP32CAN(gpio_num_t rxPin, gpio_num_t txPin, uint8_t busNum = 0);

    uint32_t begin(uint32_t baudrate) override;

    void enable() override {}
    void disable() override;

    bool sendFrame(CAN_FRAME &frame) override;

    uint32_t read(CAN_FRAME &frame) override;
    uint16_t available() override;

    void setListenOnlyMode(bool state) override;

    void setCANPins(gpio_num_t rxPin, gpio_num_t txPin);

    int setRXFilter(uint8_t, uint32_t, uint32_t, bool) { return 0; }
    int setRXFilter(uint32_t, uint32_t, bool) { return 0; }

    void setTXBufferSize(int size) { g_config.tx_queue_len = size; }
    void setRXBufferSize(int size) { g_config.rx_queue_len = size; }

    void watchFor() override {}

    void setDebuggingMode(bool) override {}

    // ==== EVENTS ======
    void pollEvents();
    bool popEvent(CAN_EVENT &evt);

    bool isInErrorState();

private:
    twai_general_config_t g_config;
    twai_timing_config_t t_config;
    twai_filter_config_t f_config;

    uint32_t currentBaudrate = 500000;

    // ==== EVENTS ======
    static const uint16_t EVENT_BUFFER_SIZE = 64;
    CAN_EVENT eventBuffer[EVENT_BUFFER_SIZE];
    volatile uint16_t eventHead = 0;
    volatile uint16_t eventTail = 0;

    inline void pushEvent(const CAN_EVENT &evt);
};