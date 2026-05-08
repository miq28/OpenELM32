#include "rgb_status.h"

#if defined(WEACT_STUDIO_CAN485_V1)

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// ===== ACTIVITY TIMERS =====
static uint32_t rxUntil = 0;
static uint32_t txUntil = 0;
static uint32_t hostUntil = 0;

// ===== ERROR =====
static bool canError = false;

// ===== WIFI STATE =====
static bool wifiEnabledState = false;
static bool wifiSTAState = false;
static bool wifiConnectedState = false;
static bool wifiClientState = false;

// ===== BLINK =====
static bool blinkState = false;
static uint32_t lastBlinkToggle = 0;

// ===== LAST OUTPUT =====
static uint8_t lastR = 255;
static uint8_t lastG = 255;
static uint8_t lastB = 255;

// =====================================================
// Helper
// =====================================================

static inline uint8_t clampAdd(uint8_t a, uint8_t b)
{
    uint16_t v = a + b;

    if (v > 255)
        return 255;

    return v;
}

static void setLED(uint8_t r, uint8_t g, uint8_t b)
{
    // avoid unnecessary updates
    if (r == lastR &&
        g == lastG &&
        b == lastB)
    {
        return;
    }

    lastR = r;
    lastG = g;
    lastB = b;

    // ESP32 neopixelWrite uses GRB order
    rgbLedWrite(RGB_LED, r, g, b);
}

// =====================================================
// Public API
// =====================================================

void rgbStatusInit()
{
    setLED(0, 0, 0);
}

void rgbCanRxActivity()
{
    rxUntil = millis() + RGB_CAN_BLINK_MS;
}

void rgbCanTxActivity()
{
    txUntil = millis() + RGB_CAN_BLINK_MS;
}

void rgbHostActivity()
{
    hostUntil = millis() + RGB_HOST_BLINK_MS;
}

void rgbCanError(bool active)
{
    canError = active;
}

void rgbSetWiFiState(
    bool wifiEnabled,
    bool isSTA,
    bool wifiConnected,
    bool clientConnected)
{
    wifiEnabledState = wifiEnabled;
    wifiSTAState = isSTA;
    wifiConnectedState = wifiConnected;
    wifiClientState = clientConnected;
}

// =====================================================
// WiFi Background Renderer
// =====================================================

static void buildWiFiBackground(
    uint8_t &r,
    uint8_t &g,
    uint8_t &b)
{
    if (!wifiEnabledState)
    {
        return;
    }

    // =================================================
    // STA MODE
    // =================================================

    if (wifiSTAState)
    {
        // disconnected -> blinking purple
        if (!wifiConnectedState)
        {
            if (!blinkState)
            {
                return;
            }

            r = 8;
            b = 10;
            return;
        }

        // connected no client
        if (!wifiClientState)
        {
            r = RGB_WIFI_DIM / 2;
            if (r > 8)
                r = 8;
            b = RGB_WIFI_DIM;
            return;
        }

        // connected + client
        r = 8;
        b = RGB_WIFI_BRIGHT;
        return;
    }

    // =================================================
    // AP MODE
    // =================================================

    // AP active no client
    if (!wifiClientState)
    {
        r = 10;
        g = 6;
        return;
    }

    // AP active + client
    r = 20;
    g = 12;
}

// =====================================================
// Main Loop
// =====================================================

void rgbStatusLoop()
{
    uint32_t now = millis();

    // ===== WIFI BLINK =====
    if ((now - lastBlinkToggle) >= RGB_WIFI_BLINK_MS)
    {
        lastBlinkToggle = now;
        blinkState = !blinkState;
    }

    // =================================================
    // ERROR OVERRIDE
    // =================================================

    if (canError)
    {
        setLED(RGB_ERROR_BRIGHTNESS, 0, 0);
        return;
    }

    // =================================================
    // BASE COLOR
    // =================================================

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    buildWiFiBackground(r, g, b);

    // =================================================
    // HOST OVERLAY
    // =================================================

    if (now < hostUntil)
    {
        r = clampAdd(r, RGB_HOST_BRIGHTNESS);
        g = clampAdd(g, RGB_HOST_BRIGHTNESS);
        b = clampAdd(b, RGB_HOST_BRIGHTNESS);
    }

    // =================================================
    // CAN OVERLAY
    // =================================================

    bool rx = (now < rxUntil);
    bool tx = (now < txUntil);

    if (rx && tx)
    {
        g = clampAdd(g, RGB_CAN_RXTX_BRIGHTNESS);
        b = clampAdd(b, RGB_CAN_RXTX_BRIGHTNESS);
    }
    else if (rx)
    {
        b = clampAdd(b, RGB_CAN_RX_BRIGHTNESS);
    }
    else if (tx)
    {
        g = clampAdd(g, RGB_CAN_TX_BRIGHTNESS);
    }

    setLED(r, g, b);
}

#endif