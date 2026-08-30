#ifndef _MODULE_MACROS_h
#define _MODULE_MACROS_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_MACROS
#define DEBUGMACROS(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGMACROS(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif

#define CONFIG_FILE_MACROS    "/config_macros.json"
#define SCENARIO_FILE_DFLT    "/macros.txt"

// Типы триггеров сценария
#define MACRO_TRIG_CRON      0   // по расписанию cron
#define MACRO_TRIG_BUTTON    1   // кнопка на веб-странице
#define MACRO_TRIG_TERM      2   // сообщение в терминале

#define MACRO_MAX_RULES      32  // максимум правил в сценарии
#define CRON_FIELD_MAX_RANGES 8  // максимум диапазонов в одном поле cron

// Структура конфига — сохраняется в config_macros.json
typedef struct {
    bool enabled;          // модуль включён
    String scenarioFile;   // путь к файлу сценария на LittleFS
} strMacrosConfig;

// Одно поле cron-выражения: список диапазонов [from..to] с шагом step
typedef struct {
    uint8_t count;
    uint8_t maxVal;
    uint8_t from[CRON_FIELD_MAX_RANGES];
    uint8_t to[CRON_FIELD_MAX_RANGES];
    uint8_t step[CRON_FIELD_MAX_RANGES];
} CronField;

// Одно правило сценария
typedef struct {
    uint8_t  type;         // MACRO_TRIG_*
    String   id;           // для button/term — токен; для cron — пусто
    String   cronExpr;     // исходное cron-выражение (для отображения)
    String   action;       // действие, выводимое в терминал при срабатывании
    bool     hasSeconds;   // cron из 6 полей (с секундами) или из 5
    CronField cron[6];     // [0]=сек [1]=мин [2]=час [3]=день [4]=месяц [5]=день-нед
    uint32_t lastFire;     // unix-время последнего срабатывания
} MacroRule;

class CLASS_MODULE_MACROS {
public:
    CLASS_MODULE_MACROS(bool _in);
#if defined(ESP32)
    void setFs(fs::LittleFSFS* fs);
#endif
#if defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

    // Публичное API
    bool reloadScenario();                       // перечитать файл сценария
    bool fireById(const String& id);             // запустить правило по id
    bool fireByType(uint8_t type, const String& id); // запустить по типу триггера
    uint8_t getRuleCount();
    void printRules();                       // вывод списка правил в терминал

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleInfo(AsyncWebServerRequest *request);
    void handleReload(AsyncWebServerRequest *request);
    void handleRun(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

    // Логика сценария
    void parseScenario();                        // разобрать строки файла в _rules
    bool parseRuleLine(const String& line, MacroRule& rule);
    void fireRule(uint8_t idx);                  // выполнить правило и вывести в терминал
    void writeDefaultScenario();                 // создать файл сценария по умолчанию
    static void tick();                          // периодическая задача cron (EERTOS)

protected:
    bool dumb;
#if defined(ESP32)
    fs::LittleFSFS* _fs;
#endif
#if defined(ESP8266)
    FS* _fs;
#endif

    strMacrosConfig _config;
    MacroRule _rules[MACRO_MAX_RULES];
    uint8_t _ruleCount;
    String _lastFiredName;    // имя последнего сработавшего правила (для веб)
    uint32_t _lastFiredAt;    // время последнего срабатывания
};

extern CLASS_MODULE_MACROS module_macros;

// Терминальные команды модуля (регистрируются через TerminalRegisterModule)
void macroTerminalRegister();

#endif // _MODULE_MACROS_h
