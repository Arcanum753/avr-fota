#ifndef _CORE_LED_TYPES_h
#define _CORE_LED_TYPES_h

// ============================================================
// core_led_types.h — типы и define'ы ядра core_led.
// Реализация: core_led.cpp (init) и core_led_engine.cpp
// (слоты и паттерны).
// ============================================================

#include <Arduino.h>

#define SLOT_MIN_TIME  100// 0.1 min time to ligth
#define LEDSTRINGLIMIT  128

// Приоритеты слотов моргания (0 = низший, выше = важнее).
// База "светодиод горит постоянно" (подключение к Wi-Fi) ниже всех слотов.
typedef enum {
    LED_PRIO_DEV = 0,       // устройства: ошибки механики
    LED_PRIO_DEMO = 1,      // демо-кассета (шаблон) — ниже OTA, не маскирует обновление
    LED_PRIO_OTA = 2,       // обновление FW/FS + прочие optional-модули (напр. шаблон)
    LED_PRIO_WIFI = 3,      // состояния Wi-Fi (высший приоритет)
    LED_PRIO_MANUAL = 4,    // терминальная команда blink (выше всех)
    LED_PRIO_COUNT = 5
} LedPriority;

// Общий паттерн ошибки устройств (слот LED_PRIO_DEV) — единый источник истины
#define LED_PATTERN_DEV_ERROR   "*.*.*.*.*"

#endif // _CORE_LED_TYPES_h
