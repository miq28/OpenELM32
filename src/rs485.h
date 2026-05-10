#pragma once

#include <Arduino.h>

class RS485Port : public Print
{
public:
    void begin(uint32_t baud);

    virtual size_t write(uint8_t c) override;
    virtual size_t write(const uint8_t *data,
                         size_t len) override;

    using Print::write;

    void printf(const char *format, ...);

    int available();
    int read();
    size_t readBytes(uint8_t *buf, size_t maxLen);

private:
    void setTX();
    void setRX();

    void enqueue(const uint8_t *data, size_t len);

    static void txTask(void *param);
};

extern RS485Port RS485;

extern int rs485_vprintf(const char *fmt,
                         va_list args);