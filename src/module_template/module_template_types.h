#ifndef _MODULE_TEMPLATE_TYPES_h
#define _MODULE_TEMPLATE_TYPES_h

// ============================================================
// module_template_types.h — типы, структуры и define'ы шаблона модуля.
// Реализация: module_template.cpp (шаблон) и
// module_template_engine.cpp (демо-логика GPIO/моргания).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

// Пины для двух GPIO, управляемых с веб-страницы
#if defined(ESP32)
#define TEMPLATE_GPIO1  32   // D32
#define TEMPLATE_GPIO2  33   // D33
#endif

#if defined(ESP8266)
#define TEMPLATE_GPIO1  16   // D0
#define TEMPLATE_GPIO2  14   // D5
#endif

#define CONFIG_FILE_TEMPLATE    "/config_template.json"

// Структура конфига — сохраняется в config_template.json
typedef struct {
    bool gpio1State;
    bool gpio2State;
    uint16_t blinkInterval;   // период моргания в мс, 0 = не моргать
    String demoSampleText;    // демо-поле, ни на что не влияет
    String demoArray[3];      // пример массива строк — см. load_config_template()
} strTmplConfig;

#endif // _MODULE_TEMPLATE_TYPES_h
