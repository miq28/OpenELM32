#pragma once

#include <Arduino.h>

class CAN_FRAME;

namespace WeActCanLogger
{
bool begin();
bool startAutoService();
void service();
bool isActive();
const char *currentPath();
uint32_t droppedFrames();

void logFrame(const CAN_FRAME &frame, uint8_t bus, bool transmitted);
} // namespace WeActCanLogger
