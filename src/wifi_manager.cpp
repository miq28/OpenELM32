#include "config.h"
#include "wifi_manager.h"
#include "gvret_comm.h"
#include "SerialConsole.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "ELM327_Emulator.h"
#include "debug.h"
#include "heap_probe.h"
#include "esp_phy_init.h"
#include "rgb_status.h"
#include "console_io.h"

#define OTA_PORT 3232
#define TELNET_PORT 23
#define OBD_PORT 35000

volatile uint32_t wifiBytesSent = 0;

static IPAddress broadcastAddr(255, 255, 255, 255);

// constructor must be here
WiFiManager::WiFiManager()
    : wifiServer(TELNET_PORT), wifiOBDII(OBD_PORT)
{
    lastBroadcast = 0;
}

// ===== OTA STATE =====
enum OTAState
{
    OTA_OFF = 0,
    OTA_READY
};

static OTAState otaState = OTA_OFF;

static bool hasAnyWiFiClient()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (SysSettings.clientNodes[i] &&
            SysSettings.clientNodes[i].connected())
        {
            return true;
        }

        if (SysSettings.wifiOBDClients[i] &&
            SysSettings.wifiOBDClients[i].connected())
        {
            return true;
        }
    }

    return false;
}

static void setHostnameEarly(const char *name)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif)
    {
        esp_netif_set_hostname(netif, name);
    }
}

// ===== OTA HANDLERS (once) =====
static void initOTAHandlers()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    ArduinoOTA
        .onStart([]()
                 {
                      String type;
                      if (ArduinoOTA.getCommand() == U_FLASH)
                         type = "sketch";
                      else // U_SPIFFS
                         type = "filesystem";

                      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
                      consolePrintln("Start updating " + type); })
        .onEnd([]()
               { consolePrintln("\nEnd"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 {
                      Serial.printf("Error[%u]: ", error);
                      if (error == OTA_AUTH_ERROR) consolePrintln("Auth Failed");
                      else if (error == OTA_BEGIN_ERROR) consolePrintln("Begin Failed");
                      else if (error == OTA_CONNECT_ERROR) consolePrintln("Connect Failed");
                      else if (error == OTA_RECEIVE_ERROR) consolePrintln("Receive Failed");
                      else if (error == OTA_END_ERROR) consolePrintln("End Failed"); });
}

// ===== OTA CONTROL =====
static void startOTA()
{
    if (otaState == OTA_READY)
        return;

    ArduinoOTA.setHostname(deviceName);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.begin();

    otaState = OTA_READY;
    DEBUG("OTA READY\n");
}

static void stopOTA()
{
    if (otaState == OTA_OFF)
        return;
    otaState = OTA_OFF;
    DEBUG("OTA STOPPED\n");
}

// ===== WIFI EVENTS =====
static void setupWiFiEvents()
{
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info)
                 {
                     DEBUG("WiFi disconnected: %d\n", info.wifi_sta_disconnected.reason);
                     stopOTA();
                     MDNS.end(); },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t)
                 {
                     DEBUG("*** WiFi GOT IP: %s ***\n", WiFi.localIP().toString().c_str());
                     DEBUG("*** HOST NAME: %s ***\n", WiFi.getHostname());

                     // ===== OTA =====
                     initOTAHandlers();
                     startOTA();

                     // ===== MDNS =====
                     if (!MDNS.begin(deviceName))
                     {
                         DEBUG("Error setting up MDNS responder!\n");
                     }
                     else
                     {
                         MDNS.addService("gvretServer", "tcp", TELNET_PORT);
                         MDNS.addService("ELM327", "tcp", OBD_PORT);
                         DEBUG("MDNS started\n");
                     } },
                 ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// ===== TCP SERVER =====
void WiFiManager::setupServer()
{
    wifiServer.begin(); // setup as a telnet server
    wifiServer.setNoDelay(true);
    consolePrintln("TCP server started");
    wifiOBDII.begin(); // setup for wifi linked ELM327 emulation
    wifiOBDII.setNoDelay(true);
}

void WiFiManager::setup()
{
    setupWiFiEvents();

    if (settings.wifiMode == 1)
    {
        consolePrintln("Wifi mode: STA");

        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true); // erase + stop
        delay(200);

        // ===== INIT LOW LEVEL (REQUIRED FOR HOSTNAME) =====
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
        {
            esp_netif_set_hostname(netif, deviceName);
        }
        WiFi.setHostname(deviceName);
        WiFi.setSleep(false);

        // WiFi.persistent(false);
        // esp_phy_erase_cal_data_in_nvs();

        WiFi.begin(settings.STA_SSID, settings.STA_PASS);
    }
    else if (settings.wifiMode == 2)
    {
        consolePrintln("Wifi mode: AP");

        WiFi.mode(WIFI_AP);
        WiFi.disconnect(true); // reset state
        delay(100);

        // set hostname on AP netif
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif)
        {
            esp_netif_set_hostname(netif, deviceName);
        }
        WiFi.setHostname(deviceName);
        WiFi.setSleep(false);
        WiFi.softAP(settings.AP_SSID, settings.AP_PASS);

        DEBUG("AP SSID: %s, PASS: %s\n", settings.AP_SSID, settings.AP_PASS);
    }
    else
    {
        consolePrintln("Wifi mode: OFF");
        WiFi.mode(WIFI_OFF);
    }

    setupServer();

    DEBUG("NET INIT DONE\n");
}

static size_t getFrameSize(const uint8_t *buf, size_t available)
{
    if (available < 2)
        return 0;

    uint8_t cmd = buf[0];

    // ---- GVRET basic parsing ----
    // Adjust if your protocol differs

    switch (cmd)
    {
    // CAN frame (most common)
    case 0xF1: // example: standard CAN frame
    case 0xF2: // example: extended CAN frame
    {
        if (available < 2)
            return 0;
        uint8_t len = buf[1];
        return len + 2; // header + payload
    }

    default:
        // fallback: treat as minimal frame
        if (available >= 2)
            return buf[1] + 2;

        return 0;
    }
}

void WiFiManager::loop()
{
    if (settings.enableBT != 0)
        return; // No wifi if BT is on

    int i;

    if (WiFi.isConnected() || settings.wifiMode == 2)
    {
        if (wifiServer.hasClient())
        {
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                if (!SysSettings.clientNodes[i] || !SysSettings.clientNodes[i].connected())
                {
                    if (SysSettings.clientNodes[i])
                        SysSettings.clientNodes[i].stop();
                    SysSettings.clientNodes[i] = wifiServer.accept();
                    if (!SysSettings.clientNodes[i])
                        DEBUG_PRINTLN("Couldn't accept client connection!");
                    else
                    {
                        DEBUG("New client: %d %s\n",
                              i,
                              SysSettings.clientNodes[i].remoteIP().toString().c_str());
                    }
                }
            }
            if (i >= MAX_CLIENTS)
            {
                // no free/disconnected spot so reject
                wifiServer.accept().stop();
            }
        }

        if (wifiOBDII.hasClient())
        {
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                if (!SysSettings.wifiOBDClients[i] || !SysSettings.wifiOBDClients[i].connected())
                {
                    if (SysSettings.wifiOBDClients[i])
                        SysSettings.wifiOBDClients[i].stop();
                    SysSettings.wifiOBDClients[i] = wifiOBDII.accept();
                    if (!SysSettings.wifiOBDClients[i])
                        consolePrintln("Couldn't accept client connection!");
                    else
                    {
                        DEBUG("New wifi ELM client: %d %s\n",
                              i,
                              SysSettings.wifiOBDClients[i].remoteIP().toString().c_str());
                    }
                }
            }
            if (i >= MAX_CLIENTS)
            {
                // no free/disconnected spot so reject
                wifiOBDII.accept().stop();
            }
        }

        // check clients for data
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            if (SysSettings.clientNodes[i] && SysSettings.clientNodes[i].connected())
            {
                if (SysSettings.clientNodes[i].available())
                {
                    // get data from the telnet client and push it to input processing
                    while (SysSettings.clientNodes[i].available())
                    {
                        uint8_t inByt;
                        inByt = SysSettings.clientNodes[i].read();
                        SysSettings.isWifiActive = true;
                        // Serial.write(inByt); //echo to serial - just for debugging. Don't leave this on!
                        gvretTCP.processIncomingByte(inByt);
                    }
                }
            }
            else
            {
                if (SysSettings.clientNodes[i])
                {
                    SysSettings.clientNodes[i].stop();
                }
            }

            if (SysSettings.wifiOBDClients[i] && SysSettings.wifiOBDClients[i].connected())
            {
                elmEmulator.setWiFiClient(&SysSettings.wifiOBDClients[i]);
                /*if(SysSettings.wifiOBDClients[i].accept())
                {
                    //get data from the telnet client and push it to input processing
                    while(SysSettings.wifiOBDClients[i].accept())
                    {
                        uint8_t inByt;
                        inByt = SysSettings.wifiOBDClients[i].read();
                        SysSettings.isWifiActive = true;
                        // gvretTCP.processIncomingByte(inByt);
                    }
                }*/
            }
            else
            {
                if (SysSettings.wifiOBDClients[i])
                {
                    SysSettings.wifiOBDClients[i].stop();
                    elmEmulator.setWiFiClient(0);
                }
            }
        }
    }

    if (WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP)
    {
        if ((micros() - lastBroadcast) > 1000000ul)
        {
            lastBroadcast = micros();
            uint8_t buff[4] = {0x1C, 0xEF, 0xAC, 0xED};
            wifiUDPServer.beginPacket(broadcastAddr, 17222);
            wifiUDPServer.write(buff, 4);
            wifiUDPServer.endPacket();
        }
    }

    rgbSetWiFiState(
        settings.wifiMode != 0,
        settings.wifiMode == 1,
        WiFi.isConnected(),
        hasAnyWiFiClient());

    ArduinoOTA.handle();
}

void WiFiManager::sendBufferedData()
{
    if (settings.enableBT != 0)
        return;

    static uint32_t lastLog = 0;
    if (millis() - lastLog > 300)
    {
        lastLog = millis();

        DEBUG("[STAT] free:%u min:%u largest:%u buf:%u\n",
              ESP.getFreeHeap(),
              heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
              tcpTxBuffer.numAvailableBytes());
    }

    // ===== SAFEGUARD #1: heap =====
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16000)
        return;

    // ===== SAFEGUARD #2: backlog =====
    if (tcpTxBuffer.numAvailableBytes() >= WIFI_BUFF_SIZE)
    {
        DEBUG("[WARN] buffer overflow protection, dropping\n");
        tcpTxBuffer.clearBufferedBytes();
        return;
    }

    static uint32_t lastSendUs = 0;
    const uint32_t nowUs = micros();

    // batching window (same concept, no memcpy)
    const uint32_t BATCH_US = 1000;

    if ((nowUs - lastSendUs) < BATCH_US)
        return;

    lastSendUs = nowUs;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!(SysSettings.clientNodes[i] && SysSettings.clientNodes[i].connected()))
            continue;

        size_t available = tcpTxBuffer.numAvailableBytes();
        if (available == 0)
            return;

        TransportEndpoint endpoint(&SysSettings.clientNodes[i]);

        size_t sent =
            tcpTxBuffer.flushToEndpoint(endpoint);

        if (sent > 0)
        {
            wifiBytesSent += sent;
        }
        else
        {
            DEBUG("[WARN][wifi] stalled send (%u bytes pending)\n",
                  (unsigned)available);
        }

        taskYIELD();
    }
}

// Utility to extract header value from headers
String getHeaderValue(String header, String headerName)
{
    return header.substring(strlen(headerName.c_str()));
}

void onOTAProgress(uint32_t progress, size_t fullSize)
{
    static int OTAcount = 0;
    // esp_task_wdt_reset();
    if (OTAcount++ == 10)
    {
        consolePrintln(progress);
        OTAcount = 0;
    }
    else
    {
        Serial.print("...");
        Serial.print(progress);
    }
}

void WiFiManager::attemptOTAUpdate()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        DEBUG("\n\n*WIFI STATUS* SSID:%s with IP Address: %s\n"
              "Attempting the OTA update from remote host\n",
              WiFi.SSID().c_str(),
              WiFi.localIP().toString().c_str());
    }
    else
    {
        DEBUG("\n\nIt appears there is no wireless connection. Cannot update.\n");
        return;
    }

    int contentLength = 0;
    bool isValidContentType = false;
    // TODO: figure out where github stores files in releases and/or how and where the images will be stored.
    int port = 80; // Non https. HTTPS would be 443 but that might not work.
    String host = String(otaHost);
    String bin = String(otaFilename);
    // esp_task_wdt_reset(); //in case watchdog was set, update it.
    Update.onProgress(onOTAProgress);

    DEBUG("Connecting to OTA server: %s\n", host.c_str());

    if (wifiClient.connect(otaHost, port))
    {
        wifiClient.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                         "Host: " + String(host) + "\r\n" +
                         "Cache-Control: no-cache\r\n" +
                         "Connection: close\r\n\r\n"); // Get the contents of the bin file

        unsigned long timeout = millis();
        while (wifiClient.available() == 0)
        {
            if (millis() - timeout > 5000)
            {
                DEBUG("Timeout trying to connect! Aborting!\n");
                wifiClient.stop();
                return;
            }
        }

        while (wifiClient.available())
        {
            String line = wifiClient.readStringUntil('\n'); // read line till /n
            line.trim();                                    // remove space, to check if the line is end of headers

            // if the the line is empty,this is end of headers break the while and feed the
            // remaining `client` to the Update.writeStream();

            if (!line.length())
            {
                break;
            }

            // Check if the HTTP Response is 200 else break and Exit Update

            if (line.startsWith("HTTP/1.1"))
            {
                if (line.indexOf("200") < 0)
                {
                    DEBUG("FAIL...Got a non 200 status code from server. Exiting OTA Update.\n");
                    break;
                }
            }

            // extract headers here
            // Start with content length

            if (line.startsWith("Content-Length: "))
            {
                contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
                consolePrintln("              ...Server indicates " + String(contentLength) + " byte file size\n");
            }

            if (line.startsWith("Content-Type: "))
            {
                String contentType = getHeaderValue(line, "Content-Type: ");
                consolePrintln("\n              ...Server indicates correct " + contentType + " payload.\n");
                if (contentType == "application/octet-stream")
                {
                    isValidContentType = true;
                }
            }
        } // end while client available
    }
    else
    {
        // Connect to remote failed
        consolePrintln("Connection to " + String(host) + " failed. Please check your setup!");
    }

    // Check what is the contentLength and if content type is `application/octet-stream`
    // consolePrintln("File length: " + String(contentLength) + ", Valid Content Type flag:" + String(isValidContentType));

    // check contentLength and content type
    if (contentLength && isValidContentType) // Check if there is enough to OTA Update
    {
        bool canBegin = Update.begin(contentLength);
        if (canBegin)
        {
            consolePrintln("There is sufficient space to update. Beginning update. \n");
            size_t written = Update.writeStream(wifiClient);

            if (written == contentLength)
            {
                consolePrintln("\nWrote " + String(written) + " bytes to memory...");
            }
            else
            {
                consolePrintln("\n********** FAILED - Wrote:" + String(written) + " of " + String(contentLength) + ". Try again later. ********\n\n");
                return;
            }

            if (Update.end())
            {
                //  consolePrintln("OTA file transfer completed!");
                if (Update.isFinished())
                {
                    consolePrintln("Rebooting new firmware...\n");
                    ESP.restart();
                }
                else
                    consolePrintln("FAILED...update not finished? Something went wrong!");
            }
            else
            {
                consolePrintln("Error Occurred. Error #: " + String(Update.getError()));
                return;
            }
        } // end if can begin
        else
        {
            // not enough space to begin OTA
            // Understand the partitions and space availability

            consolePrintln("Not enough space to begin OTA");
            wifiClient.clear();
        }
    } // End contentLength && isValidContentType
    else
    {
        consolePrintln("There was no content in the response");
        wifiClient.clear();
    }
}
