#pragma once
// Native shim: capability-tagged heap allocations become plain malloc.
// SIM_NO_PSRAM=1 makes large PSRAM-only requests (64 KB and up: the
// full-size Clawd's sprite buffers) fail, as on a C6, so the no-PSRAM paths
// (cell_sprite.h) can be checked here. The sim's own LVGL draw buffers
// (PSRAM-tagged, under 64 KB) still allocate.
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1u << 0)
#define MALLOC_CAP_INTERNAL (1u << 1)
#define MALLOC_CAP_8BIT     (1u << 2)
#define MALLOC_CAP_DMA      (1u << 3)

static inline void* heap_caps_malloc(size_t size, unsigned caps) {
    static int no_psram = -1;
    if (no_psram < 0) no_psram = getenv("SIM_NO_PSRAM") != NULL;
    if (no_psram && caps == MALLOC_CAP_SPIRAM && size >= 64 * 1024) return NULL;
    return malloc(size);
}
static inline void heap_caps_free(void* ptr) { free(ptr); }
