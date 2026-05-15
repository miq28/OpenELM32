/*
 ESP32RET.ino

 Created: June 1, 2020
 Author: Collin Kidder

Copyright (c) 2014-2020 Collin Kidder, Michael Neuweiler

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "Arduino.h"
#include "config.h"
#include <esp32_can.h>
#include <Preferences.h>
#include "ELM327_Emulator.h"
#include "SerialConsole.h"
#include "wifi_manager.h"
#include "gvret_comm.h"
#include "can_manager.h"
#include "lawicel.h"
#include "debug.h"
#include "esp_flash.h"
#include "Logger.h"
#include "rgb_status.h"
#include "console_io.h"

const char *resetReasonToStr(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:
        return "POWERON"; // Power on or RST pin toggled
    case ESP_RST_EXT:
        return "EXTERNAL (EN pin)"; // External pin - not applicable for ESP32
    case ESP_RST_SW:
        return "SOFTWARE"; // Software reset via esp_restart
    case ESP_RST_PANIC:
        return "PANIC"; // Exception/panic/crash
    case ESP_RST_INT_WDT:
        return "INT WDT"; // Interrupt watchdog (software or hardware)
    case ESP_RST_TASK_WDT:
        return "TASK WDT"; // Task watchdog
    case ESP_RST_WDT:
        return "OTHER WDT"; // Other watchdog
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP"; // Reset after exiting deep sleep mode
    case ESP_RST_BROWNOUT:
        return "BROWNOUT"; // Brownout reset (software or hardware)
    case ESP_RST_SDIO:
        return "SDIO"; // Reset over SDIO
    case ESP_RST_USB:
        return "USB"; // Reset by USB peripheral
    case ESP_RST_JTAG:
        return "JTAG"; // Reset by JTAG
    case ESP_RST_EFUSE:
        return "EFUSE"; // Reset due to efuse error
    case ESP_RST_PWR_GLITCH:
        return "POWER_GLITCH"; // Reset due to power glitch detected
    case ESP_RST_CPU_LOCKUP:
        return "CPU_LOCKUP"; // Reset due to CPU lock up (double exception)
    default:
        return "UNKNOWN"; // Reset reason can not be determined
    }
}

void printPrefs()
{
    consolePrintf("===== Preferences =====\n");
    consolePrintf("wifiMode=%u\n", prefs.getUChar("wifiMode", 0));
    consolePrintf("AP_SSID=%s\n", prefs.getString("AP_SSID", "").c_str());
    consolePrintf("AP_PASS=%s\n", prefs.getString("AP_PASS", "").c_str());
    consolePrintf("STA_SSID=%s\n", prefs.getString("STA_SSID", "").c_str());
    consolePrintf("STA_PASS=%s\n", prefs.getString("STA_PASS", "").c_str());
    consolePrintf("binarycomm=%d\n", prefs.getBool("binarycomm", false));
    consolePrintf("loglevel=%u\n", prefs.getUChar("loglevel", 0));
    consolePrintf("enable-bt=%d\n", prefs.getBool("enable-bt", false));
    consolePrintf("enableLawicel=%d\n", prefs.getBool("enableLawicel", false));
    consolePrintf("sendingBus=%d\n", prefs.getInt("sendingBus", 0));
    consolePrintf("btname=%s\n", prefs.getString("btname", "").c_str());
    consolePrintf("dbg_en=%d\n", prefs.getBool("dbg_en", false));
    consolePrintf("dbg_ser=%d\n", prefs.getBool("dbg_ser", false));
    consolePrintf("dbg_485=%d\n", prefs.getBool("dbg_485", false));
    consolePrintf("systype=%u\n", prefs.getUChar("systype", 0));

    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        char key[32];

        sprintf(key, "can%ispeed", i);
        consolePrintf("%s=%u\n", key, prefs.getUInt(key, 0));

        sprintf(key, "can%i_en", i);
        consolePrintf("%s=%d\n", key, prefs.getBool(key, false));

        sprintf(key, "can%i-listenonly", i);
        consolePrintf("%s=%d\n", key, prefs.getBool(key, false));

        sprintf(key, "can%i-fdspeed", i);
        consolePrintf("%s=%u\n", key, prefs.getUInt(key, 0));

        sprintf(key, "can%i-fdmode", i);
        consolePrintf("%s=%d\n", key, prefs.getBool(key, false));
    }

    consolePrintf("=======================\n");
}

void checkESPBoard()
{
    // ===== BASIC =====
    consolePrintf("SDK=%s\n", ESP.getSdkVersion());
    consolePrintf("Chip=%s Rev %u\n", ESP.getChipModel(), ESP.getChipRevision());
    consolePrintf("Cores=%u\n", ESP.getChipCores());

    // ===== CLOCK =====
    consolePrintf("CPU=%d MHz\n", getCpuFrequencyMhz());
    consolePrintf("XTAL=%d MHz\n", getXtalFrequencyMhz());
    consolePrintf("APB=%.1f MHz\n", getApbFrequency() / 1000000.0);

    // ===== MAC =====
    uint64_t mac64 = ESP.getEfuseMac();

    consolePrintf("MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  (uint8_t)(mac64 >> 40),
                  (uint8_t)(mac64 >> 32),
                  (uint8_t)(mac64 >> 24),
                  (uint8_t)(mac64 >> 16),
                  (uint8_t)(mac64 >> 8),
                  (uint8_t)(mac64 >> 0));

    // ===== CHIP ID (derived) =====
    uint32_t chipId = 0;

    for (int i = 0; i < 17; i += 8)
    {
        chipId |= ((mac64 >> (40 - i)) & 0xFF) << i;
    }

    consolePrintf("ChipID=%u (0x%08X)\n", chipId, chipId);

    // ===== FLASH =====
    uint32_t flash_id;

    if (esp_flash_read_id(NULL, &flash_id) == ESP_OK)
    {
        consolePrintf("FLASH_ID=%06X\n", flash_id);
    }
    else
    {
        consolePrintf("FLASH_ID=read failed\n");
    }

    consolePrintf("FlashSpeed=%.1f MHz\n",
                  ESP.getFlashChipSpeed() / 1000000.0);
    consolePrintf("FlashSize=%.2f MB\n",
                  ESP.getFlashChipSize() / (1024.0 * 1024));
    consolePrintf("FlashMode=%d (0=QIO,1=QOUT,2=DIO,3=DOUT)\n",
                  ESP.getFlashChipMode());

    // ===== RAM =====
    consolePrintf("HeapTotal=%.2f KB\n",
                  ESP.getHeapSize() / 1024.0);
    consolePrintf("HeapFree=%.2f KB\n",
                  ESP.getFreeHeap() / 1024.0);
    consolePrintf("HeapMaxAlloc=%.2f KB\n",
                  ESP.getMaxAllocHeap() / 1024.0);

    // ===== PSRAM =====
    if (psramFound())
    {
        consolePrintf("PSRAMTotal=%.2f KB\n",
                      ESP.getPsramSize() / 1024.0);
        consolePrintf("PSRAMFree=%.2f KB\n",
                      ESP.getFreePsram() / 1024.0);
    }
    else
    {
        consolePrintf("PSRAM=not found\n");
    }
}

// on the S3 we want the default pins to be different
#ifdef CONFIG_IDF_TARGET_ESP32S3
MCP2517FD CAN1(10, 3);
#endif

byte i = 0;

uint32_t lastFlushMicros = 0;

bool markToggle[6];
uint32_t lastMarkTrigger = 0;

EEPROMSettings settings;
SystemSettings SysSettings;
Preferences prefs;
char deviceName[32];
char otaHost[40];
char otaFilename[100];

uint8_t espChipRevision;

ELM327Emu elmEmulator;

WiFiManager wifiManager;

CommBuffer usbTxBuffer;
CommBuffer tcpTxBuffer;

GVRET_Comm_Handler gvretUSB;
GVRET_Comm_Handler gvretTCP;

CANManager canManager; // keeps track of bus load and abstracts away some details of how things are done
LAWICELHandler lawicel;

SerialConsole console;

CAN_COMMON *canBuses[NUM_BUSES];

void statsTask(void *param);

void statsTask(void *param)
{
    uint32_t lastTime = millis();

    while (true)
    {
        uint32_t now = millis();
        uint32_t dt = now - lastTime;

        if (dt >= 1000)
        {
            lastTime = now;

            // ---- SAFE SNAPSHOT (avoid race issues) ----
            uint32_t bits = 0;
            uint32_t busPct = 0;

            float heap_kb = esp_get_free_heap_size() / 1024.0f;
            float min_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT) / 1024.0f;

            // DEBUG("[CAN] RX:%lu/s drop:%lu/s buf:%u%% max:%u%% | TX a:%lu ok:%lu f:%lu d:%lu b:%lu h:%.1fkB (min:%.1fkB)\n",
            consolePrintf("[STATS] h:%.1fkB (min:%.1fkB)\n",
                  //   CANRxBuffer::getRateRx(),
                  //   CANRxBuffer::getRateDrop(),
                  //   usage_pct,
                  //   max_pct,
                  //   CANTxBuffer::getRateAttempt(),
                  //   CANTxBuffer::getRateOk(),
                  //   CANTxBuffer::getRateFail(),
                  //   CANTxBuffer::getRateDrop(),
                  //   CANTxBuffer::getRateBlock(),
                  heap_kb,
                  min_kb);
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // yield CPU
    }
}

void buildDeviceName(char *out, size_t outSize, const char *baseName)
{
    uint64_t chipid = ESP.getEfuseMac();            // 48-bit MAC
    uint16_t shortId = (uint16_t)(chipid & 0xFFFF); // last 2 bytes

    // Format: BASE_XXXX
    snprintf(out, outSize, "%s_%04X", baseName, shortId);
}

// initializes all the system EEPROM values. Chances are this should be broken out a bit but
// there is only one checksum check for all of them so it's simple to do it all here.
void loadSettings()
{
    Logger::console("Loading settings....");

    gvretUSB.setBuffer(&usbTxBuffer);
    gvretTCP.setBuffer(&tcpTxBuffer);

    lawicel.setOutputBuffer(&usbTxBuffer);

#if defined(WEACT_STUDIO_CAN485_V1)
#define BASE_DEVICE_NAME "WEACT_CAN485"
// #elif defined(WAVESHARE_ESP32_S3_RS485_CAN)
#elif CONFIG_IDF_TARGET_ESP32
#define BASE_DEVICE_NAME "ESP32"
#elif CONFIG_IDF_TARGET_ESP32S3
#define BASE_DEVICE_NAME "ESP32S3"
#endif

    buildDeviceName(deviceName, sizeof(deviceName), BASE_DEVICE_NAME);

    consolePrintf("deviceName=%s\n", deviceName);

    // Logger::console("%i\n", espChipRevision);

    for (int i = 0; i < NUM_BUSES; i++)
        canBuses[i] = nullptr;

    prefs.begin(PREF_NAME, false);
    // prefs.clear();

    if (!prefs.isKey("wifiMode"))
        prefs.putUChar("wifiMode", 1);
    if (!prefs.isKey("AP_SSID"))
        prefs.putString("AP_SSID", deviceName);
    if (!prefs.isKey("AP_PASS"))
        prefs.putString("AP_PASS", "12345678");
    if (!prefs.isKey("STA_SSID"))
        prefs.putString("STA_SSID", "RUT906_8818");
    if (!prefs.isKey("STA_PASS"))
        prefs.putString("STA_PASS", "Gz25FePc");

    settings.wifiMode = prefs.getUChar("wifiMode");
    prefs.getString("AP_SSID", settings.AP_SSID, sizeof(settings.AP_SSID));
    prefs.getString("AP_PASS", settings.AP_PASS, sizeof(settings.AP_PASS));
    prefs.getString("STA_SSID", settings.STA_SSID, sizeof(settings.STA_SSID));
    prefs.getString("STA_PASS", settings.STA_PASS, sizeof(settings.STA_PASS));

    settings.useBinarySerialComm = prefs.getBool("binarycomm", false);
    settings.logLevel = prefs.getUChar("loglevel", 1); // info
    settings.wifiMode = prefs.getUChar("wifiMode", 2); // Wifi defaults to creating an AP
    settings.enableBT = prefs.getBool("enable-bt", false);
    settings.enableLawicel = prefs.getBool("enableLawicel", true);
    settings.enableVirtualOBD = prefs.getBool("virtualOBD", false);
    settings.sendingBus = prefs.getInt("sendingBus", 0);

    if (prefs.getString("btname", settings.btName, 32) == 0)
    {
        strcpy(settings.btName, "ELM327-");
        strcat(settings.btName, deviceName);
    }

    debug_enabled = prefs.getBool("dbg_en", true);
    debug_to_serial = prefs.getBool("dbg_ser", false);
    debug_to_rs485 = prefs.getBool("dbg_485", true);

    uint8_t defaultVal;
#if CONFIG_IDF_TARGET_ESP32
    defaultVal = 1;
#elif CONFIG_IDF_TARGET_ESP32S3
    defaultVal = 3;
#else
#error "=== UNKNOWN BOARD! ==="
#endif

    settings.systemType = prefs.getUChar("systype", defaultVal);

#if defined(WEACT_STUDIO_CAN485_V1)
    settings.systemType == 1;
    Logger::console("Running on WEACT_STUDIO_CAN485_V1");
    canBuses[0] = &CAN0;
    CAN0.setCANPins(CAN_RX, CAN_TX);
    // canBuses[1] = &CAN1;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelMode = false;
    SysSettings.lawicellExtendedMode = false;
    SysSettings.lawicelTimestamping = false;
    SysSettings.numBuses = NUM_BUSES;
    SysSettings.isWifiActive = false;
    SysSettings.isWifiConnected = false;
    // strcpy(deviceName, "WEACT_CAN485");
    strcpy(otaHost, "media3.evtv.me");
    strcpy(otaFilename, "/weact_can485.bin");
#elif CONFIG_IDF_TARGET_ESP32
    settings.systemType == defaultVal;
    Logger::console("Running on EVTV ESP32 Board");
    canBuses[0] = &CAN0;
    CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5);
    // canBuses[1] = &CAN1;
    SysSettings.LED_CANTX = 255;
    SysSettings.LED_CANRX = 255;
    SysSettings.LED_LOGGING = 255;
    SysSettings.LED_CONNECTION_STATUS = 255;
    SysSettings.fancyLED = false;
    SysSettings.logToggle = false;
    SysSettings.txToggle = true;
    SysSettings.rxToggle = true;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelMode = false;
    SysSettings.lawicellExtendedMode = false;
    SysSettings.lawicelTimestamping = false;
    SysSettings.numBuses = NUM_BUSES;
    SysSettings.isWifiActive = false;
    SysSettings.isWifiConnected = false;
    strcpy(deviceName, EVTV_NAME);
    strcpy(otaHost, "media3.evtv.me");
    strcpy(otaFilename, "/esp32ret.bin");
#elif CONFIG_IDF_TARGET_ESP32S3
    settings.systemType == defaultVal;
    Logger::console("Running on EVTV ESP32-S3 Board");
    canBuses[0] = &CAN0;
    // canBuses[1] = &CAN1;
    // CAN1.setINTPin(3);
    // CAN1.setCSPin(10);
    SysSettings.LED_CANTX = 255; // 18;
    SysSettings.LED_CANRX = 255; // 18;
    SysSettings.LED_LOGGING = 255;
    SysSettings.LED_CONNECTION_STATUS = 255;
    SysSettings.fancyLED = false;
    SysSettings.logToggle = false;
    SysSettings.txToggle = true;
    SysSettings.rxToggle = true;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelMode = false;
    SysSettings.lawicellExtendedMode = false;
    SysSettings.lawicelTimestamping = false;
    SysSettings.numBuses = NUM_BUSES;
    SysSettings.isWifiActive = false;
    SysSettings.isWifiConnected = false;
    strcpy(deviceName, EVTV_NAME);
    strcpy(otaHost, "media3.evtv.me");
    strcpy(otaFilename, "/esp32s3ret.bin");
#else

    if (settings.systemType == 0)
    {
        Logger::console("Running on Macchina A0");
        canBuses[0] = &CAN0;
        SysSettings.LED_CANTX = 255;
        SysSettings.LED_CANRX = 255;
        SysSettings.LED_LOGGING = 255;
        SysSettings.LED_CONNECTION_STATUS = 0;
        SysSettings.fancyLED = true;
        SysSettings.logToggle = false;
        SysSettings.txToggle = true;
        SysSettings.rxToggle = true;
        SysSettings.lawicelAutoPoll = false;
        SysSettings.lawicelMode = false;
        SysSettings.lawicellExtendedMode = false;
        SysSettings.lawicelTimestamping = false;
        SysSettings.numBuses = NUM_BUSES;
        SysSettings.isWifiActive = false;
        SysSettings.isWifiConnected = false;
        strcpy(deviceName, MACC_NAME);
        strcpy(otaHost, "macchina.cc");
        strcpy(otaFilename, "/a0/files/a0ret.bin");
        pinMode(13, OUTPUT);
        digitalWrite(13, LOW);
        delay(100);
        FastLED.addLeds<LED_TYPE, A0_LED_PIN, COLOR_ORDER>(leds, A0_NUM_LEDS).setCorrection(TypicalLEDStrip);
        FastLED.setBrightness(LED_BRIGHTNESS);
        leds[0] = CRGB::Red;
        FastLED.show();
        pinMode(21, OUTPUT);
        digitalWrite(21, LOW);
        CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5);
    }
#endif

    char buff[80];
    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        sprintf(buff, "can%ispeed", i);
        settings.canSettings[i].nomSpeed = prefs.getUInt(buff, 500000);
        sprintf(buff, "can%i_en", i);
        settings.canSettings[i].enabled = prefs.getBool(buff, (i < 2) ? true : false);
        sprintf(buff, "can%i-listenonly", i);
        settings.canSettings[i].listenOnly = prefs.getBool(buff, false);
        sprintf(buff, "can%i-fdspeed", i);
        settings.canSettings[i].fdSpeed = prefs.getUInt(buff, 5000000);
        sprintf(buff, "can%i-fdmode", i);
        settings.canSettings[i].fdMode = prefs.getBool(buff, false);
    }

    printPrefs();

    prefs.end();

    Logger::setLoglevel((Logger::LogLevel)settings.logLevel);

    for (int rx = 0; rx < NUM_BUSES; rx++)
        SysSettings.lawicelBusReception[rx] = true; // default to showing messages on RX

    consolePrintf("deviceName=%s\n", deviceName);
    consolePrintf("binarycomm=%d\n", settings.useBinarySerialComm);
}

void setup()
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // for the ESP32S3 it will block if nothing is connected to USB and that can slow down the program
    // if nothing is connected. But, you can't set 0 or writing rapidly to USB will lose data. It needs
    // some sort of timeout but I'm not sure exactly how much is needed or if there is a better way
    // to deal with this issue.
    Serial.setTxTimeoutMs(2);
#endif
    Serial.begin(1000000); // for production
    RS485.begin(1000000);
    debug_to_serial = true;
    consolePrintf("\n=== BOOT ===\n");
    esp_reset_reason_t r = esp_reset_reason();
    consolePrintf("Reset reason=%s (%d)\n",
          resetReasonToStr(r), r);

    float heap_kb = esp_get_free_heap_size() / 1024.0f;
    float min_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT) / 1024.0f;
    consolePrintf("HeapBeforeSetup=%.1f kB (min:%.1f kB)\n",
          heap_kb,
          min_kb);

    checkESPBoard();

    espChipRevision = ESP.getChipRevision();

    consolePrintf("BuildNumber=%d\n", CFG_BUILD_NUM);

    SysSettings.isWifiConnected = false;

    loadSettings();

    printEEPROMSettings(settings);
    printSystemSettings(SysSettings);

    // CAN0.setDebuggingMode(true);
    // CAN1.setDebuggingMode(true);

    if (settings.enableBT)
    {
        consolePrintln("Starting bluetooth");
        elmEmulator.setup();
    }

    /*else*/ wifiManager.setup();

    canManager.setup();

    SysSettings.lawicelMode = false;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelTimestamping = false;
    SysSettings.lawicelPollCounter = 0;

    rgbStatusInit();

    // elmEmulator.setup();

    // xTaskCreatePinnedToCore(
    //     statsTask,
    //     "statsTask",
    //     4096,
    //     nullptr,
    //     1,
    //     nullptr,
    //     0 // run on Core 0 (keeps CAN on Core 1 cleaner)
    // );

    heap_kb = esp_get_free_heap_size() / 1024.0f;
    min_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT) / 1024.0f;
    consolePrintf("HeapAfterSetup=%.1f kB (min:%.1f kB)\n",
          heap_kb,
          min_kb);

    consolePrint("Done with init\n");
    debug_to_serial = false;
}

/*
Send a fake frame out USB and maybe to file to show where the mark was triggered at. The fake frame has bits 31 through 3
set which can never happen in reality since frames are either 11 or 29 bit IDs. So, this is a sign that it is a mark frame
and not a real frame. The bottom three bits specify which mark triggered.
*/
void sendMarkTriggered(int which)
{
    CAN_FRAME frame;
    frame.id = 0xFFFFFFF8ull + which;
    frame.extended = true;
    frame.length = 0;
    frame.rtr = 0;
    canManager.displayFrame(frame, 0);
}

/*
Loop executes as often as possible all the while interrupts fire in the background.
The serial comm protocol is as follows:
All commands start with 0xF1 this helps to synchronize if there were comm issues
Then the next byte specifies which command this is.
Then the command data bytes which are specific to the command
Lastly, there is a checksum byte just to be sure there are no missed or duped bytes
Any bytes between checksum and 0xF1 are thrown away

Yes, this should probably have been done more neatly but this way is likely to be the
fastest and safest with limited function calls
*/
void loop()
{
    // uint32_t temp32;
    bool isConnected = false;
    int serialCnt;
    uint8_t in_byte;

    /*if (Serial)*/ isConnected = true;

    if (SysSettings.lawicelPollCounter > 0)
        SysSettings.lawicelPollCounter--;
    //}

    canManager.loop();
    /*if (!settings.enableBT)*/ wifiManager.loop();

    size_t tcpLength = tcpTxBuffer.numAvailableBytes();
    size_t usbLength = usbTxBuffer.numAvailableBytes();
    size_t maxLength = (tcpLength > usbLength) ? tcpLength : usbLength;

    // If the max time has passed or the buffer is almost filled then send buffered data out
    if ((micros() - lastFlushMicros > SER_BUFF_FLUSH_INTERVAL) || (maxLength > (WIFI_BUFF_SIZE - 40)))
    {
        lastFlushMicros = micros();
        if (usbLength > 0)
        {
            TransportEndpoint endpoint(&Serial);

            usbTxBuffer.flushToEndpoint(endpoint);
        }
        if (tcpLength > 0)
        {
            wifiManager.sendBufferedData();
        }
    }

    // ===== USB SERIAL INPUT =====
    serialCnt = 0;

    while ((Serial.available() > 0) && serialCnt < 128)
    {
        serialCnt++;

        in_byte = Serial.read();

        gvretUSB.processIncomingByte(in_byte);
    }

    // ===== RS485 INPUT =====
    serialCnt = 0;

    while ((RS485.available() > 0) && serialCnt < 128)
    {
        serialCnt++;

        in_byte = RS485.read();

        gvretUSB.processIncomingByte(in_byte);
    }

    rgbStatusLoop();

    elmEmulator.loop();
}
