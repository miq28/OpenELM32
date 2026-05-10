#pragma once

#include <Arduino.h>

class CAN_FRAME;
class CommBuffer;

class LAWICELHandler
{
public:
    void setOutputBuffer(CommBuffer *buf);
    void handleLongCmd(char *buffer);
    void handleShortCmd(char cmd);
    void sendFrameToBuffer(CAN_FRAME &frame, int whichBus);

private:
    CommBuffer *outputBuffer = nullptr;
    char tokens[14][10];

    void tokenizeCmdString(char *buff);
    void uppercaseToken(char *token);
    void printBusName(int bus);
    bool parseLawicelCANCmd(CAN_FRAME &frame);
};