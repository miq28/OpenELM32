#pragma once

#include "Arduino.h"

#if defined(WEACT_STUDIO_CAN485_V1)

void rgbStatusInit();
void rgbStatusLoop();

void rgbCanRxActivity();
void rgbCanTxActivity();
void rgbCanError(bool active);

#else

inline void rgbStatusInit() {}
inline void rgbStatusLoop() {}

inline void rgbCanRxActivity() {}
inline void rgbCanTxActivity() {}
inline void rgbCanError(bool active) {}

#endif