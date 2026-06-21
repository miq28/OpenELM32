#include "weact_can_logger.h"
#include "config.h"

#if defined(WEACT_STUDIO_CAN485_V1)

#include "esp32_can.h"
#include "weact_sd.h"
#include "debug.h"
#include <FS.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stddef.h>

namespace
{
constexpr char LOG_DIRECTORY[] = "/can_logs";
constexpr uint32_t RECORD_MAGIC = 0x314E4143; // "CAN1"
constexpr uint16_t RECORD_END = 0xA55A;
constexpr uint16_t LOG_VERSION = 1;
constexpr size_t WRITE_BATCH_SIZE = 16;
constexpr size_t LOG_QUEUE_LENGTH = 64;

struct __attribute__((packed)) LogHeader
{
    char magic[8];
    uint16_t version;
    uint16_t headerSize;
    uint16_t recordSize;
    uint8_t reserved[16];
    uint16_t crc;
};

struct __attribute__((packed)) LogRecord
{
    uint32_t magic;
    uint32_t sequence;
    uint64_t timestampUs;
    uint32_t id;
    uint8_t bus;
    uint8_t flags;
    uint8_t length;
    uint8_t reserved;
    uint8_t data[8];
    uint16_t crc;
    uint16_t endMarker;
};

static_assert(sizeof(LogHeader) == 32, "Unexpected CAN log header size");
static_assert(sizeof(LogRecord) == 36, "Unexpected CAN log record size");

File logFile;
QueueHandle_t logQueue = nullptr;
TaskHandle_t writerTaskHandle = nullptr;
char logPath[32] = {};
volatile bool active = false;
volatile bool storageFault = false;
uint32_t dropped = 0;
uint32_t sequence = 0;
uint32_t writtenRecords = 0;
uint32_t dirtyRecords = 0;
uint32_t lastFlushMs = 0;
uint32_t lastRecordStatusMs = 0;
portMUX_TYPE droppedMux = portMUX_INITIALIZER_UNLOCKED;
bool autoServiceEnabled = false;
bool serviceStartedReported = false;
bool cardAbsentReported = false;
bool loggerFailureReported = false;
uint32_t lastServiceAttemptMs = 0;

uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    while (length--)
    {
        crc ^= static_cast<uint16_t>(*data++) << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }

    return crc;
}

bool selectLogPath()
{
    if (!WeActSD::exists(LOG_DIRECTORY) && !WeActSD::mkdir(LOG_DIRECTORY))
        return false;

    for (uint32_t number = 1; number <= 99999; number++)
    {
        snprintf(logPath, sizeof(logPath), "%s/LOG%05lu.BIN",
                 LOG_DIRECTORY, static_cast<unsigned long>(number));
        if (!WeActSD::exists(logPath))
            return true;
    }

    logPath[0] = 0;
    return false;
}

bool writeHeader()
{
    LogHeader header = {};
    memcpy(header.magic, "CANLOG1", 7);
    header.version = LOG_VERSION;
    header.headerSize = sizeof(LogHeader);
    header.recordSize = sizeof(LogRecord);
    header.crc = crc16(reinterpret_cast<const uint8_t *>(&header),
                       offsetof(LogHeader, crc));

    if (logFile.write(reinterpret_cast<const uint8_t *>(&header),
                      sizeof(header)) != sizeof(header))
        return false;

    logFile.flush();
    return true;
}

void writerTask(void *)
{
    LogRecord records[WRITE_BATCH_SIZE];

    for (;;)
    {
        if (!active)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t count = 0;
        if (xQueueReceive(logQueue, &records[count], pdMS_TO_TICKS(100)) == pdTRUE)
        {
            count++;
            while (count < WRITE_BATCH_SIZE &&
                   xQueueReceive(logQueue, &records[count], 0) == pdTRUE)
                count++;

            const size_t bytes = count * sizeof(LogRecord);
            if (logFile.write(reinterpret_cast<const uint8_t *>(records), bytes) != bytes)
            {
                DEBUG("[SD] write failed; card may have been removed\n");
                logFile.close();
                xQueueReset(logQueue);
                storageFault = true;
                active = false;
                continue;
            }

            writtenRecords += count;
            dirtyRecords += count;
        }

        const uint32_t now = millis();
        if (dirtyRecords > 0 && now - lastFlushMs >= 500)
        {
            logFile.flush();
            dirtyRecords = 0;
            lastFlushMs = now;
        }

        if (now - lastRecordStatusMs >= 2000)
        {
            if (dirtyRecords > 0)
            {
                logFile.flush();
                dirtyRecords = 0;
                lastFlushMs = now;
            }

            if (!WeActSD::probe())
            {
                DEBUG("[SD] card removed or media probe failed\n");
                logFile.close();
                xQueueReset(logQueue);
                storageFault = true;
                active = false;
                continue;
            }

            lastRecordStatusMs = now;
            DEBUG("[SD] recording active: %lu records, %lu dropped\n",
                  static_cast<unsigned long>(writtenRecords),
                  static_cast<unsigned long>(WeActCanLogger::droppedFrames()));
        }
    }
}

} // namespace

namespace WeActCanLogger
{
bool begin()
{
    if (active)
        return true;
    if (!WeActSD::isMounted() || !selectLogPath())
        return false;

    logFile = WeActSD::open(logPath, FILE_WRITE);
    if (!logFile)
        return false;

    logFile.setBufferSize(1024);
    if (!writeHeader())
    {
        logFile.close();
        return false;
    }

    if (!logQueue || !writerTaskHandle)
    {
        logFile.close();
        return false;
    }

    xQueueReset(logQueue);
    dropped = 0;
    sequence = 0;
    writtenRecords = 0;
    dirtyRecords = 0;
    lastFlushMs = millis();
    lastRecordStatusMs = 0;
    storageFault = false;
    active = true;
    return true;
}

bool startAutoService()
{
    if (!logQueue)
    {
        logQueue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogRecord));
        if (!logQueue)
            return false;
    }

    if (!writerTaskHandle &&
        xTaskCreatePinnedToCore(writerTask, "can_sd_writer", 4096, nullptr,
                                1, &writerTaskHandle, 1) != pdPASS)
    {
        vQueueDelete(logQueue);
        logQueue = nullptr;
        return false;
    }

    autoServiceEnabled = true;
    return true;
}

void service()
{
    if (!autoServiceEnabled)
        return;

    if (active)
        return;

    const uint32_t now = millis();

    if (now < 2000)
        return;

    if (storageFault)
    {
        DEBUG("[SD] card/write fault; resetting SD interface\n");
        WeActSD::end();
        storageFault = false;
        cardAbsentReported = false;
        loggerFailureReported = false;
        lastServiceAttemptMs = 0;
    }

    if (lastServiceAttemptMs != 0 &&
        now - lastServiceAttemptMs < 30000)
        return;
    lastServiceAttemptMs = now;

    if (!serviceStartedReported)
    {
        DEBUG("[SD] service started; checking for card\n");
        serviceStartedReported = true;
    }

    if (!WeActSD::isMounted())
    {
        if (WeActSD::begin())
        {
            DEBUG("[SD] card mounted: %.1f MB total\n",
                  WeActSD::totalBytes() / (1024.0 * 1024.0));
            cardAbsentReported = false;
        }
        else if (!cardAbsentReported)
        {
            DEBUG("[SD] card not detected; retrying every 30 seconds\n");
            cardAbsentReported = true;
        }
    }

    if (WeActSD::isMounted())
    {
        if (begin())
        {
            DEBUG("[SD] recording started: %s heap=%lu bytes\n",
                  currentPath(),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
            loggerFailureReported = false;
        }
        else if (!loggerFailureReported)
        {
            DEBUG("[SD] mounted, but log file creation failed\n");
            loggerFailureReported = true;
        }
    }
}

bool isActive()
{
    return active;
}

const char *currentPath()
{
    return logPath;
}

uint32_t droppedFrames()
{
    portENTER_CRITICAL(&droppedMux);
    const uint32_t count = dropped;
    portEXIT_CRITICAL(&droppedMux);
    return count;
}

void logFrame(const CAN_FRAME &frame, uint8_t bus, bool transmitted)
{
    if (!active || !logQueue)
        return;

    LogRecord record = {};
    record.magic = RECORD_MAGIC;
    record.sequence = sequence++;
    record.timestampUs = static_cast<uint64_t>(esp_timer_get_time());
    record.id = frame.id;
    record.bus = bus;
    record.flags = (frame.extended ? 0x01 : 0x00) |
                   (frame.rtr ? 0x02 : 0x00) |
                   (transmitted ? 0x04 : 0x00);
    record.length = frame.length <= sizeof(record.data)
                        ? frame.length
                        : sizeof(record.data);
    memcpy(record.data, frame.data.uint8, record.length);
    record.crc = crc16(reinterpret_cast<const uint8_t *>(&record),
                       offsetof(LogRecord, crc));
    record.endMarker = RECORD_END;

    if (xQueueSend(logQueue, &record, 0) != pdTRUE)
    {
        portENTER_CRITICAL(&droppedMux);
        dropped++;
        portEXIT_CRITICAL(&droppedMux);
    }
}
} // namespace WeActCanLogger

#else

namespace WeActCanLogger
{
bool begin() { return false; }
bool startAutoService() { return false; }
void service() {}
bool isActive() { return false; }
const char *currentPath() { return ""; }
uint32_t droppedFrames() { return 0; }
void logFrame(const CAN_FRAME &, uint8_t, bool) {}
} // namespace WeActCanLogger

#endif
