#pragma once

#include <Arduino.h>
#include <FS.h>

namespace WeActSD
{
bool begin(uint32_t frequency = 20000000);
void end();

bool isMounted();
uint8_t cardType();
uint64_t totalBytes();
uint64_t usedBytes();
bool probe();

File open(const char *path, const char *mode = FILE_READ);
bool exists(const char *path);
bool mkdir(const char *path);
bool remove(const char *path);
} // namespace WeActSD
