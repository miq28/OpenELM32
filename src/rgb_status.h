#pragma once

#include <Arduino.h>

#if defined(WEACT_STUDIO_CAN485_V1)

void rgbStatusInit();
void rgbStatusLoop();

void rgbCanRxActivity();
void rgbCanTxActivity();

void rgbHostActivity();

void rgbSetWiFiState(
    bool wifiEnabled,
    bool isSTA,
    bool wifiConnected,
    bool clientConnected);

void rgbCanError(bool active);

// ===== TIMING =====
static constexpr uint32_t RGB_CAN_BLINK_MS = 12;
static constexpr uint32_t RGB_HOST_BLINK_MS = 15;
static constexpr uint32_t RGB_WIFI_BLINK_MS = 700;

// ===== BRIGHTNESS =====
static constexpr uint8_t RGB_WIFI_DIM = 0; // was 8
static constexpr uint8_t RGB_WIFI_BRIGHT = 20;

static constexpr uint8_t RGB_CAN_RX_BRIGHTNESS = 30;
static constexpr uint8_t RGB_CAN_TX_BRIGHTNESS = 30;
static constexpr uint8_t RGB_CAN_RXTX_BRIGHTNESS = 40;

static constexpr uint8_t RGB_HOST_BRIGHTNESS = 18;

static constexpr uint8_t RGB_ERROR_BRIGHTNESS = 35;

#else

inline void rgbStatusInit() {}
inline void rgbStatusLoop() {}

inline void rgbCanRxActivity() {}
inline void rgbCanTxActivity() {}

inline void rgbHostActivity() {}

inline void rgbSetWiFiState(
    bool,
    bool,
    bool,
    bool)
{
}

inline void rgbCanError(bool) {}

#endif