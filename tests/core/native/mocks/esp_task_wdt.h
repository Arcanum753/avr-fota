#ifndef _MOCK_ESP_TASK_WDT_H
#define _MOCK_ESP_TASK_WDT_H
#include <stdint.h>
static inline int esp_task_wdt_init(uint32_t, bool) { return 0; }
static inline int esp_task_wdt_reset() { return 0; }
static inline int esp_task_wdt_add(void*) { return 0; }
static inline int esp_task_wdt_delete(void*) { return 0; }
#endif
