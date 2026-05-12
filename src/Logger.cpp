/*
 * Logger.cpp
 *
 Copyright (c) 2013 Collin Kidder, Michael Neuweiler, Charles Galpin

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

#include "Logger.h"
#include "config.h"
#include "debug.h"

Logger::LogLevel Logger::logLevel = Logger::Info;
uint32_t Logger::lastLogTime = 0;

static uint8_t *appendBuffer = nullptr;
static uint16_t appendLength = 0;
static size_t appendMax = 0;

static inline bool appendFormatted(
    const char *fmt,
    ...)
{
    if (!appendBuffer)
    {
        return false;
    }

    if (appendLength >= appendMax)
    {
        return false;
    }

    va_list args;

    va_start(args, fmt);

    int written = vsnprintf(
        (char *)&appendBuffer[appendLength],
        appendMax - appendLength,
        fmt,
        args);

    va_end(args);

    if (written <= 0)
    {
        return false;
    }

    size_t remaining =
        appendMax - appendLength;

    if ((size_t)written >= remaining)
    {
        appendLength = appendMax;
        return false;
    }

    appendLength += written;

    return true;
}

/*
 * Output a debug message with a variable amount of parameters.
 * printf() style, see Logger::log()
 *
 */
void Logger::debug(const char *message, ...)
{
    if (logLevel > Debug)
    {
        return;
    }

    va_list args;
    va_start(args, message);
    Logger::log(Debug, message, args);
    va_end(args);
}

/*
 * Output a info message with a variable amount of parameters
 * printf() style, see Logger::log()
 */
void Logger::info(const char *message, ...)
{
    if (logLevel > Info)
    {
        return;
    }

    va_list args;
    va_start(args, message);
    Logger::log(Info, message, args);
    va_end(args);
}

/*
 * Output a warning message with a variable amount of parameters
 * printf() style, see Logger::log()
 */
void Logger::warn(const char *message, ...)
{
    if (logLevel > Warn)
    {
        return;
    }

    va_list args;
    va_start(args, message);
    Logger::log(Warn, message, args);
    va_end(args);
}

/*
 * Output a error message with a variable amount of parameters
 * printf() style, see Logger::log()
 */
void Logger::error(const char *message, ...)
{
    if (logLevel > Error)
    {
        return;
    }

    va_list args;
    va_start(args, message);
    Logger::log(Error, message, args);
    va_end(args);
}

/*
 * Output a console message with a variable amount of parameters
 * printf() style, see Logger::logMessage()
 */
void Logger::console(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    Logger::logMessage(message, args);
    va_end(args);
}

/*
 * Set the log level. Any output below the specified log level will be omitted.
 */
void Logger::setLoglevel(LogLevel level)
{
    logLevel = level;
}

/*
 * Retrieve the current log level.
 */
Logger::LogLevel Logger::getLogLevel()
{
    return logLevel;
}

/*
 * Return a timestamp when the last log entry was made.
 */
uint32_t Logger::getLastLogTime()
{
    return lastLogTime;
}

/*
 * Returns if debug log level is enabled. This can be used in time critical
 * situations to prevent unnecessary string concatenation (if the message won't
 * be logged in the end).
 *
 * Example:
 * if (Logger::isDebug()) {
 *    Logger::debug("current time: %d", millis());
 * }
 */
boolean Logger::isDebug()
{
    return logLevel == Debug;
}

/*
 * Output a log message (called by debug(), info(), warn(), error(), console())
 *
 * Supports printf() like syntax:
 *
 * %% - outputs a '%' character
 * %s - prints the next parameter as string
 * %d - prints the next parameter as decimal
 * %f - prints the next parameter as double float
 * %x - prints the next parameter as hex value
 * %X - prints the next parameter as hex value with '0x' added before
 * %b - prints the next parameter as binary value
 * %B - prints the next parameter as binary value with '0b' added before
 * %l - prints the next parameter as long
 * %c - prints the next parameter as a character
 * %t - prints the next parameter as boolean ('T' or 'F')
 * %T - prints the next parameter as boolean ('true' or 'false')
 */
void Logger::log(LogLevel level, const char *format, va_list args)
{
    uint8_t buffer[256];

    appendBuffer = buffer;
    appendLength = 0;
    appendMax = sizeof(buffer);

    lastLogTime = millis();

    appendFormatted("%lu - ", lastLogTime);

    switch (level)
    {
    case Debug:
        appendFormatted("DEBUG");
        break;

    case Info:
        appendFormatted("INFO");
        break;

    case Warn:
        appendFormatted("WARNING");
        break;

    case Error:
        appendFormatted("ERROR");
        break;
    }

    appendFormatted(": ");

    logMessage(format, args);
}

/*
 * Output a log message (called by log(), console())
 *
 * Supports printf() like syntax:
 *
 * %% - outputs a '%' character
 * %s - prints the next parameter as string
 * %d - prints the next parameter as decimal
 * %f - prints the next parameter as double float
 * %x - prints the next parameter as hex value
 * %X - prints the next parameter as hex value with '0x' added before
 * %l - prints the next parameter as long
 * %c - prints the next parameter as a character
 * %t - prints the next parameter as boolean ('T' or 'F')
 * %T - prints the next parameter as boolean ('true' or 'false')
 */
void Logger::logMessage(const char *format, va_list args)
{
    uint8_t buffer[200];

    appendBuffer = buffer;
    appendLength = 0;
    appendMax = sizeof(buffer);
    uint8_t writeLen;

    for (; *format != 0; ++format)
    {
        if (*format == '%')
        {
            ++format;

            if (*format == '\0')
            {
                break;
            }

            if (*format == '%')
            {
                appendFormatted("%%");
                continue;
            }

            if (*format == 's')
            {
                char *s = va_arg(args, char *);
                appendFormatted("%s", s);
                continue;
            }

            if (*format == 'd' || *format == 'i')
            {
                appendFormatted("%i", va_arg(args, int));
                continue;
            }

            if (*format == 'f')
            {
                appendFormatted("%.2f", va_arg(args, double));
                continue;
            }

            if (*format == 'x')
            {
                appendFormatted("%X", va_arg(args, int));
                continue;
            }

            if (*format == 'X')
            {
                appendFormatted("0x%X", va_arg(args, int));
                continue;
            }

            if (*format == 'l')
            {
                ++format;

                if (*format == 'd')
                {
                    appendFormatted("%ld", va_arg(args, long));
                    continue;
                }

                if (*format == 'u')
                {
                    appendFormatted("%lu", va_arg(args, unsigned long));
                    continue;
                }

                if (*format == 'x')
                {
                    appendFormatted("%lX", va_arg(args, unsigned long));
                    continue;
                }
            }

            if (*format == 'c')
            {
                appendFormatted("%c", va_arg(args, int));
                continue;
            }

            if (*format == 't')
            {
                appendFormatted("%c", (va_arg(args, int) == 1) ? 'T' : 'F');

                continue;
            }

            if (*format == 'T')
            {
                appendFormatted(va_arg(args, int) == 1 ? "TRUE" : "FALSE");

                continue;
            }
        }
        else
            appendFormatted("%c", *format);
    }
    appendFormatted("\r\n");

    if (debug_to_serial)
    {
        Serial.write(buffer, appendLength);
    }

    if (debug_to_rs485)
    {
        RS485.write(buffer, appendLength);
    }

    // If wifi has connected nodes then send to them too.
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (SysSettings.clientNodes[i] && SysSettings.clientNodes[i].connected())
        {
            SysSettings.clientNodes[i].write(buffer, appendLength);
        }
    }
}
