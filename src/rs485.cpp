#include "rs485.h"
#include "config.h"
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static HardwareSerial RS485Serial(2);
RS485Port RS485;

// ===== TX QUEUE =====
typedef struct
{
    uint16_t len;
    uint8_t data[256];
} tx_item_t;

static QueueHandle_t txQueue;
static SemaphoreHandle_t uartMutex;   // ✅ NEW

// ===== STDOUT HOOK (DISABLED - keep it disabled) =====
// extern "C" int _write(...) { ... }

// ===== INIT =====
void RS485Port::begin(uint32_t baud)
{
    pinMode((int)RS485_DE, OUTPUT);
    digitalWrite((int)RS485_DE, LOW);

    RS485Serial.begin(baud, SERIAL_8N1, (int)RS485_RO, (int)RS485_DI);

    txQueue = xQueueCreate(64, sizeof(tx_item_t));
    uartMutex = xSemaphoreCreateMutex();   // ✅ NEW

    xTaskCreatePinnedToCore(
        txTask,
        "rs485_tx",
        8192,
        this,
        1,
        NULL,
        1);
}

// ===== LOW LEVEL =====
void RS485Port::setTX()
{
    digitalWrite((int)RS485_DE, HIGH);
}

void RS485Port::setRX()
{
    digitalWrite((int)RS485_DE, LOW);
}

// ===== ENQUEUE =====
void RS485Port::enqueue(const uint8_t *data, size_t len)
{
    if (!txQueue) return;

    tx_item_t item;
    item.len = len > sizeof(item.data) ? sizeof(item.data) : len;
    memcpy(item.data, data, item.len);

    xQueueSend(txQueue, &item, 0); // drop if full
}

// ===== PRINT API =====
void RS485Port::print(const char *str)
{
    if (!str) return;
    enqueue((const uint8_t *)str, strlen(str));
}

void RS485Port::println(const char *str)
{
    if (!str) return;
    enqueue((const uint8_t *)str, strlen(str));
    enqueue((const uint8_t *)"\r\n", 2);
}

void RS485Port::printf(const char *format, ...)
{
    if (!format) return;

    char buffer[256];

    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len <= 0) return;

    if (len >= (int)sizeof(buffer))
        len = sizeof(buffer) - 1;

    enqueue((uint8_t *)buffer, len);
}

void RS485Port::write(const uint8_t *data, size_t len)
{
    enqueue(data, len);
}

// ===== TX TASK =====
void RS485Port::txTask(void *param)
{
    RS485Port *self = (RS485Port *)param;
    tx_item_t item;

    static UBaseType_t minWatermark = 9999;

    while (true)
    {
        // ===== stack monitor =====
        UBaseType_t w = uxTaskGetStackHighWaterMark(NULL);
        if (w < minWatermark)
        {
            minWatermark = w;
            Serial.printf("TX STACK MIN: %u\n", w);
        }

        if (xQueueReceive(txQueue, &item, portMAX_DELAY))
        {
            self->setTX();

            // ✅ LOCK UART
            xSemaphoreTake(uartMutex, portMAX_DELAY);

            RS485Serial.write(item.data, item.len);

            while (uxQueueMessagesWaiting(txQueue))
            {
                if (xQueueReceive(txQueue, &item, 0))
                {
                    RS485Serial.write(item.data, item.len);
                }
            }

            RS485Serial.flush();

            // ✅ UNLOCK UART
            xSemaphoreGive(uartMutex);

            self->setRX();
        }
    }
}

// ===== READ =====
int RS485Port::available()
{
    int v;
    xSemaphoreTake(uartMutex, portMAX_DELAY);
    v = RS485Serial.available();
    xSemaphoreGive(uartMutex);
    return v;
}

int RS485Port::read()
{
    int v;
    xSemaphoreTake(uartMutex, portMAX_DELAY);
    v = RS485Serial.read();
    xSemaphoreGive(uartMutex);
    return v;
}

size_t RS485Port::readBytes(uint8_t *buf, size_t maxLen)
{
    size_t n = 0;

    xSemaphoreTake(uartMutex, portMAX_DELAY);

    while (RS485Serial.available() && n < maxLen)
    {
        int b = RS485Serial.read();
        if (b < 0) break;
        buf[n++] = (uint8_t)b;
    }

    xSemaphoreGive(uartMutex);

    return n;
}

// ===== printf hook (safe, but avoid using globally) =====
int rs485_vprintf(const char *fmt, va_list args)
{
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);

    if (len > 0)
    {
        if (len >= (int)sizeof(buffer))
            len = sizeof(buffer) - 1;

        RS485.write((uint8_t *)buffer, len);
    }
    return len;
}