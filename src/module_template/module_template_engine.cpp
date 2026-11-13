#include "core_web/FSWebServerLib.h"

#include "module_template.h"
#include "core_sys/eertos.h"
#include "core_led/core_led.h"

#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif

// ============================================================
// Паттерн демо-моргания (кассета модуля)
// ============================================================

static const char patTemplateDemo[] PROGMEM = "*...*...*";

// ============================================================
// Светодиодная индикация (демо-кассета)
// ============================================================

void CLASS_MODULE_TEMPLATE::ledMacrosTemplateDemo() {
    // Пример: модуль сам носит свою кассету моргания. Слот LED_PRIO_DEMO ниже OTA,
    // поэтому демо-моргание не маскирует индикацию обновления.
    // При копировании шаблона заменить на свою логику и свой паттерн.
    ledSetState(LED_PRIO_DEMO, patTemplateDemo, 3);
}

// ============================================================
// Демо-логика GPIO/моргания
// ============================================================

void CLASS_MODULE_TEMPLATE::applyGpioState() {

    digitalWrite(TEMPLATE_GPIO1, _config.gpio1State ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2, _config.gpio2State ? HIGH : LOW);
}

void CLASS_MODULE_TEMPLATE::applyGpioTarget() {
    // Единая политика для всех путей (веб и терминал): при включённом
    // моргании вывод обновит ближайший тик blinkTimerTask, иначе — сразу.
    if (_config.blinkInterval == 0) {
        applyGpioState();
    }
}

void CLASS_MODULE_TEMPLATE::applyBlinkTimer() {
    // Старый таймер снимаем всегда: SetTimerTask идемпотентен по указателю,
    // но сброс _blinkState гарантирует старт новой настройки с первой фазы.
    DelTimerTask(blinkTimerTask);
    _blinkState = false;

    if (_config.blinkInterval > 0) {
        SetTimerTask(blinkTimerTask, _config.blinkInterval);
        return;
    }

    // Моргание выключено — сразу выставляем статическое состояние выходов.
    applyGpioState();
}

void CLASS_MODULE_TEMPLATE::blinkTimerTask() {
    if (module_template._config.blinkInterval == 0) {
        module_template.applyGpioState();
        return;
    }

    module_template._blinkState = !module_template._blinkState;

    digitalWrite(TEMPLATE_GPIO1,
    (module_template._blinkState && module_template._config.gpio1State) ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2,
    (module_template._blinkState && module_template._config.gpio2State) ? HIGH : LOW);
    module_template.ledMacrosTemplateDemo(); // пример вызова кассеты из периодической задачи
    SetTimerTask(blinkTimerTask, module_template._config.blinkInterval);
}
