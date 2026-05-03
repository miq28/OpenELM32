#pragma once
#include <Arduino.h>
#include "esp_heap_caps.h"
#include "debug.h"

typedef struct {
    uint32_t t_us;
    uint32_t free8;
    uint32_t min8;
    uint32_t largest8;
    uint32_t free32;
    uint32_t largest32;
} heap_snap_t;

static inline heap_snap_t heap_snap()
{
    heap_snap_t s;
    s.t_us      = micros();
    s.free8     = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s.min8      = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    s.largest8  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.free32    = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    s.largest32 = heap_caps_get_largest_free_block(MALLOC_CAP_32BIT);
    return s;
}

static inline void heap_print(const char* tag, const heap_snap_t& s)
{
    DEBUG(
        "[HEAP][%s] t:%u free8:%u min8:%u largest8:%u free32:%u largest32:%u\n",
        tag, s.t_us, s.free8, s.min8, s.largest8, s.free32, s.largest32
    );
}