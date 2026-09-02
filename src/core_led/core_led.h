#ifndef _COMMON_GPIO_h
#define _COMMON_GPIO_h


#ifdef DEBUG_LED
#define DEBUGLOGLED(...) DBG_MOD("[C_LED] ", __VA_ARGS__)
#else
#define DEBUGLOGLED(...)
#endif

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

void ledInit();
void ledSetState(LedPriority prio, const char* pattern, int16_t times); // times: -1 бесконечно, 0 = снять слот
void ledClearState(LedPriority prio);
void ledSetSteady(bool on); // базовое состояние «горит постоянно» (подключено к Wi-Fi)

void espLedOn ();
void espLedOff ();

void ledMacroTimerTask() ;
bool ledMacroBlinker( ) ;
void ledMacroRst();
void LedMacroSet (const char* _inStr, int16_t _times);

#endif // _COMMON_GPIO_h
