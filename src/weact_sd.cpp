#include "weact_sd.h"
#include "config.h"
#include "debug.h"

#if defined(WEACT_STUDIO_CAN485_V1)

#include <SD.h>
#include <SPI.h>

namespace
{
SPIClass sdSpi(VSPI);
bool mounted = false;
}

namespace WeActSD
{
bool begin(uint32_t frequency)
{
    if (mounted)
        return true;

    const uint32_t heapBefore = ESP.getFreeHeap();
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    mounted = SD.begin(SD_CS, sdSpi, frequency, "/sd", 1, false);

    if (!mounted)
    {
        DEBUG("[SD] SD.begin failed: CS=%d SCK=%d MOSI=%d MISO=%d frequency=%lu\n",
              static_cast<int>(SD_CS), static_cast<int>(SD_SCK),
              static_cast<int>(SD_MOSI), static_cast<int>(SD_MISO),
              static_cast<unsigned long>(frequency));
        SD.end();
        sdSpi.end();
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        DEBUG("[SD] SPI initialized, but card type is CARD_NONE\n");
        SD.end();
        sdSpi.end();
        mounted = false;
    }

    if (mounted)
    {
        DEBUG("[SD] mount heap: before=%lu after=%lu used=%lu bytes\n",
              static_cast<unsigned long>(heapBefore),
              static_cast<unsigned long>(ESP.getFreeHeap()),
              static_cast<unsigned long>(heapBefore - ESP.getFreeHeap()));
    }

    return mounted;
}

void end()
{
    if (!mounted)
        return;

    SD.end();
    sdSpi.end();
    mounted = false;
}

bool isMounted()
{
    return mounted;
}

uint8_t cardType()
{
    return mounted ? SD.cardType() : CARD_NONE;
}

uint64_t totalBytes()
{
    return mounted ? SD.totalBytes() : 0;
}

uint64_t usedBytes()
{
    return mounted ? SD.usedBytes() : 0;
}

bool probe()
{
    if (!mounted)
        return false;

    uint8_t sector[512];
    return SD.readRAW(sector, 0);
}

File open(const char *path, const char *mode)
{
    return mounted ? SD.open(path, mode) : File();
}

bool exists(const char *path)
{
    return mounted && SD.exists(path);
}

bool mkdir(const char *path)
{
    return mounted && SD.mkdir(path);
}

bool remove(const char *path)
{
    return mounted && SD.remove(path);
}
} // namespace WeActSD

#else

namespace WeActSD
{
bool begin(uint32_t)
{
    return false;
}

void end()
{
}

bool isMounted()
{
    return false;
}

uint8_t cardType()
{
    return 0;
}

uint64_t totalBytes()
{
    return 0;
}

uint64_t usedBytes()
{
    return 0;
}

bool probe()
{
    return false;
}

File open(const char *, const char *)
{
    return File();
}

bool exists(const char *)
{
    return false;
}

bool mkdir(const char *)
{
    return false;
}

bool remove(const char *)
{
    return false;
}
} // namespace WeActSD

#endif
