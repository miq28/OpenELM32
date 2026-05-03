#pragma once
#include <Arduino.h>

class RS485Port
{
public:
    void begin(uint32_t baud);

    void print(const char *str);
    void println(const char *str);
    void printf(const char *format, ...);

    void write(const uint8_t *data, size_t len);

    int available();
    int read();
    size_t readBytes(uint8_t* buf, size_t maxLen);

private:
    void setTX();
    void setRX();

    void enqueue(const uint8_t *data, size_t len);
    static void txTask(void *param);
};

extern RS485Port RS485;
extern int rs485_vprintf(const char *fmt, va_list args);