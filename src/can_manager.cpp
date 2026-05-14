#include <Arduino.h>
#include "can_manager.h"
#include "esp32_can.h"
#include "config.h"
#include "SerialConsole.h"
#include "gvret_comm.h"
#include "lawicel.h"
#include "ELM327_Emulator.h"
#include "debug.h"
#include "rgb_status.h"

// twai alerts copied here for ease of access. Look up alerts right here:
// #define TWAI_ALERT_TX_IDLE                  0x00000001  /**< Alert(1): No more messages to transmit */
// #define TWAI_ALERT_TX_SUCCESS               0x00000002  /**< Alert(2): The previous transmission was successful */
// #define TWAI_ALERT_RX_DATA                  0x00000004  /**< Alert(4): A frame has been received and added to the RX queue */
// #define TWAI_ALERT_BELOW_ERR_WARN           0x00000008  /**< Alert(8): Both error counters have dropped below error warning limit */
// #define TWAI_ALERT_ERR_ACTIVE               0x00000010  /**< Alert(16): TWAI controller has become error active */
// #define TWAI_ALERT_RECOVERY_IN_PROGRESS     0x00000020  /**< Alert(32): TWAI controller is undergoing bus recovery */
// #define TWAI_ALERT_BUS_RECOVERED            0x00000040  /**< Alert(64): TWAI controller has successfully completed bus recovery */
// #define TWAI_ALERT_ARB_LOST                 0x00000080  /**< Alert(128): The previous transmission lost arbitration */
// #define TWAI_ALERT_ABOVE_ERR_WARN           0x00000100  /**< Alert(256): One of the error counters have exceeded the error warning limit */
// #define TWAI_ALERT_BUS_ERROR                0x00000200  /**< Alert(512): A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus */
// #define TWAI_ALERT_TX_FAILED                0x00000400  /**< Alert(1024): The previous transmission has failed (for single shot transmission) */
// #define TWAI_ALERT_RX_QUEUE_FULL            0x00000800  /**< Alert(2048): The RX queue is full causing a frame to be lost */
// #define TWAI_ALERT_ERR_PASS                 0x00001000  /**< Alert(4096): TWAI controller has become error passive */
// #define TWAI_ALERT_BUS_OFF                  0x00002000  /**< Alert(8192): Bus-off condition occurred. TWAI controller can no longer influence bus */
// #define TWAI_ALERT_RX_FIFO_OVERRUN          0x00004000  /**< Alert(16384): An RX FIFO overrun has occurred */
// #define TWAI_ALERT_TX_RETRIED               0x00008000  /**< Alert(32768): An message transmission was cancelled and retried due to an errata workaround */
// #define TWAI_ALERT_PERIPH_RESET             0x00010000  /**< Alert(65536): The TWAI controller was reset */
// #define TWAI_ALERT_ALL                      0x0001FFFF  /**< Bit mask to enable all alerts during configuration */
// #define TWAI_ALERT_NONE                     0x00000000  /**< Bit mask to disable all alerts during configuration */
// #define TWAI_ALERT_AND_LOG                  0x00020000  /**< Bit mask to enable alerts to also be logged when they occur. Note that logging from the ISR is disabled if CONFIG_TWAI_ISR_IN_IRAM is enabled (see docs). */

static bool hasWifiClientConnected()
{
    for (int c = 0; c < MAX_CLIENTS; c++)
    {
        if (SysSettings.clientNodes[c] &&
            SysSettings.clientNodes[c].connected())
        {
            return true;
        }
    }

    return false;
}

static inline size_t getOutputBacklog(bool sendToConsole)
{
    size_t wifiLength =
        hasWifiClientConnected()
            ? tcpTxBuffer.numAvailableBytes()
            : 0;

    size_t serialLength =
        sendToConsole
            ? usbTxBuffer.numAvailableBytes()
            : 0;

    return (wifiLength > serialLength)
               ? wifiLength
               : serialLength;
}

CANManager::CANManager()
{
    sendToConsole = true;
}

void CANManager::setup()
{
    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        if (settings.canSettings[i].enabled)
        {
            if ((settings.canSettings[i].fdMode == 0) || !canBuses[i]->supportsFDMode())
            {
                canBuses[i]->begin(settings.canSettings[i].nomSpeed);
                Serial.printf("Enabled CAN%u with speed %u\n", i, settings.canSettings[i].nomSpeed);
                if ((i == 0) && (settings.systemType == 2))
                {
                    digitalWrite(SW_EN, HIGH); // MUST be HIGH to use CAN0 channel
                    Serial.println("Enabling SWCAN Mode");
                }
                if ((i == 1) && (settings.systemType == 2))
                {
                    digitalWrite(SW_EN, LOW); // MUST be LOW to use CAN1 channel
                    Serial.println("Enabling CAN1 will force CAN0 off.");
                }
                // no need to do this for the built-in CAN
                if (i > 0)
                    canBuses[i]->enable();
            }
            else
            {
                canBuses[i]->beginFD(settings.canSettings[i].nomSpeed, settings.canSettings[i].fdSpeed);
                Serial.printf("Enabled CAN1 In FD Mode With Nominal Speed %u and Data Speed %u",
                              settings.canSettings[i].nomSpeed, settings.canSettings[i].fdSpeed);
                canBuses[i]->enable();
            }

            if (settings.canSettings[i].listenOnly)
            {
                canBuses[i]->setListenOnlyMode(true);
            }
            else
            {
                canBuses[i]->setListenOnlyMode(false);
            }
            canBuses[i]->watchFor();
        }
        else
        {
            canBuses[i]->disable();
        }
    }
}

void CANManager::sendFrame(CAN_COMMON *bus, CAN_FRAME &frame)
{
    bus->sendFrame(frame);
}

void CANManager::sendFrame(CAN_COMMON *bus, CAN_FRAME_FD &frame)
{
    bus->sendFrameFD(frame);
}

void CANManager::displayFrame(CAN_FRAME &frame, int whichBus)
{
    if (settings.enableLawicel && SysSettings.lawicelMode)
    {
        lawicel.sendFrameToBuffer(frame, whichBus);
    }
    else
    {
        if (hasWifiClientConnected())
        {
            tcpTxBuffer.sendFrameToBuffer(frame, whichBus);
        }
        else if (sendToConsole)
        {
            usbTxBuffer.sendFrameToBuffer(frame, whichBus);
        }
    }
}

void CANManager::displayFrame(CAN_FRAME_FD &frame, int whichBus)
{
    if (settings.enableLawicel && SysSettings.lawicelMode)
    {
        // lawicel.sendFrameToBuffer(frame, whichBus);
        DEBUG_PRINTLN("lawfd");
    }
    else
    {
        if (hasWifiClientConnected())
        {
            tcpTxBuffer.sendFrameToBuffer(frame, whichBus);
        }
        else if (sendToConsole)
        {
            usbTxBuffer.sendFrameToBuffer(frame, whichBus);
        }
    }
}

void CANManager::loop()
{
    CAN_FRAME incoming;
    CAN_FRAME_FD inFD;

    // ===== STATS =====
    static uint32_t rxFrames = 0;
    static uint32_t forwardedFrames = 0;
    static uint32_t rxFullCount = 0;
    static uint32_t lastStats = 0;

    static uint32_t prevUsbDrops = 0;
    static uint32_t prevTcpDrops = 0;

    size_t maxLength = getOutputBacklog(sendToConsole);

    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        if (!canBuses[i])
            continue;

        if (!settings.canSettings[i].enabled)
            continue;

        ESP32CAN *esp = static_cast<ESP32CAN *>(canBuses[i]);

        // ===== ALWAYS POLL EVENTS =====
        esp->pollEvents();

        // ===== RGB ERROR STATE =====
        rgbCanError(esp->isInErrorState());

        // ===== EVENT HANDLING =====
        CAN_EVENT evt;
        while (esp->popEvent(evt))
        {
            // aggregate noisy overflow event
            if (evt.alerts & TWAI_ALERT_RX_QUEUE_FULL)
            {
                rxFullCount++;
                continue;
            }

            // print meaningful events only
            char alertStr[64];
            alertsToText(evt.alerts, alertStr, sizeof(alertStr));

            DEBUG("[CAN EVT] %s st:%s rx:%u tx:%u\n",
                  alertStr,
                  stateToStr(evt.state),
                  evt.rx_err,
                  evt.tx_err);
        }

        // ===== FRAME RX =====
        uint32_t loopCount = 0;

        while (maxLength < (WIFI_BUFF_SIZE - 80))
        {
            bool gotFrame = false;

            if (settings.canSettings[i].fdMode == 0)
            {
                // direct non-blocking read
                if (canBuses[i]->read(incoming))
                {
                    gotFrame = true;

                    rxFrames++;

                    displayFrame(incoming, i);

                    forwardedFrames++;

                    if (i == settings.sendingBus)
                    {
                        elmEmulator.processCANReply(incoming);
                    }
                }
            }
            else
            {
                if (canBuses[i]->readFD(inFD))
                {
                    gotFrame = true;

                    rxFrames++;

                    displayFrame(inFD, i);

                    forwardedFrames++;
                }
            }

            // no more frames available
            if (!gotFrame)
                break;

            // allow lower-priority tasks (RGB/WiFi/etc) to run
            // every 64 CAN frames we briefly yield scheduler control
            if ((++loopCount & 0x3F) == 0)
            {
                taskYIELD();
            }

            // refresh output pressure
            maxLength = getOutputBacklog(sendToConsole);
        }
    }

    // ===== PERIODIC STATS =====
    uint32_t now = millis();

    if ((now - lastStats) >= 1000)
    {
        uint32_t dt = now - lastStats;
        lastStats = now;

        uint32_t fpsIn =
            (dt > 0) ? ((rxFrames * 1000ul) / dt) : 0;

        uint32_t fpsOut =
            (dt > 0) ? ((forwardedFrames * 1000ul) / dt) : 0;

        uint32_t wifiKBs = wifiBytesSent / 1024;
        wifiBytesSent = 0;

        uint32_t uptimeSec = millis() / 1000;
        uint32_t hrs = uptimeSec / 3600;
        uint32_t mins = (uptimeSec % 3600) / 60;
        uint32_t secs = uptimeSec % 60;

        // ===== monitor commbuffer drops ====
        uint32_t usbDrops = usbTxBuffer.getDroppedFrames();

        uint32_t tcpDrops = tcpTxBuffer.getDroppedFrames();

        uint32_t usbDropRate = usbDrops - prevUsbDrops;

        uint32_t tcpDropRate = tcpDrops - prevTcpDrops;

        prevUsbDrops = usbDrops;
        prevTcpDrops = tcpDrops;

        // ===== adaptive ASCII overload control =====
        extern uint32_t asciiThreshold;

        static uint32_t stableSeconds = 0;

        if (rxFullCount > 0)
        {
            stableSeconds = 0;

            // gentle reduction
            if (asciiThreshold > 256)
                asciiThreshold -= 128;

            if (asciiThreshold < 256)
                asciiThreshold = 256;
        }
        else
        {
            stableSeconds++;

            // only increase after several stable seconds
            if (stableSeconds >= 5)
            {
                stableSeconds = 0;

                if (asciiThreshold < 1500)
                    asciiThreshold += 32;
            }
        }

        DEBUG("[CAN STAT] in:%lu fps out:%lu fps usbDrop:%lu/s tcpDrop:%lu/s rxqovf:%lu/s wifi:%u tx:%lu KB/s heap:%u at:%u up:%02lu:%02lu:%02lu\n",
              fpsIn,
              fpsOut,
              usbDropRate,
              tcpDropRate,
              rxFullCount,
              tcpTxBuffer.numAvailableBytes(),
              wifiKBs,
              ESP.getFreeHeap(),
              asciiThreshold,
              hrs,
              mins,
              secs);

        rxFrames = 0;
        forwardedFrames = 0;
        rxFullCount = 0;
    }
}
