#include "rgb_status.h"

#if defined(WEACT_STUDIO_CAN485_V1)

#include <Adafruit_NeoPixel.h>
#include "config.h"

static Adafruit_NeoPixel pixel(1, RGB_LED, NEO_GRB + NEO_KHZ800);

static volatile uint32_t rxUntil = 0;
static volatile uint32_t txUntil = 0;
static volatile bool errorState = false;

static constexpr uint32_t BLINK_TIME_MS = 30;

static void applyColor(uint8_t r, uint8_t g, uint8_t b)
{
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

void rgbStatusInit()
{
    pixel.begin();
    pixel.clear();
    pixel.show();
}

void rgbCanRxActivity()
{
    rxUntil = millis() + BLINK_TIME_MS;
}

void rgbCanTxActivity()
{
    txUntil = millis() + BLINK_TIME_MS;
}

void rgbCanError(bool active)
{
    errorState = active;
}

void rgbStatusLoop()
{
    uint32_t now = millis();

    // Highest priority
    if (errorState)
    {
        applyColor(255, 0, 0);
        return;
    }

    bool rxActive = (int32_t)(rxUntil - now) > 0;
    bool txActive = (int32_t)(txUntil - now) > 0;

    // RX + TX simultaneously
    if (rxActive && txActive)
    {
        applyColor(0, 255, 255);
    }
    else if (rxActive)
    {
        applyColor(0, 0, 255);
    }
    else if (txActive)
    {
        applyColor(0, 255, 0);
    }
    else
    {
        applyColor(0, 0, 0);
    }
}

#endif