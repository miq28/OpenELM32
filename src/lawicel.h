#pragma once

#include <Arduino.h>

class CAN_FRAME;

class LAWICELHandler
{
public:
    void setOutput(Stream *s);
    void handleLongCmd(char *buffer);
    void handleShortCmd(char cmd);
    void sendFrameToBuffer(CAN_FRAME &frame, int whichBus);

private:
    Stream *output = &Serial;
    char tokens[14][10];

    void tokenizeCmdString(char *buff);
    void uppercaseToken(char *token);
    void printBusName(int bus);
    bool parseLawicelCANCmd(CAN_FRAME &frame);
};