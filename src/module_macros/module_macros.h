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

#include "tcl.h"
#include "ccronexpr.h"

#define CONFIG_FILE_MACROS    "/config_macros.json"
#define MACROS_DIR            "/macros"
#define MACROS_DIR_RE         "/macros/"
#define MACRO_DEFAULT_NAME    "new_macros.tcl"

// Ограничения прототипа (ESP8266 RAM ограничен — лимиты ниже)
#if defined(ESP32)
#define MACRO_MAX_FILES       16     // максимум файлов-сценариев в списке
#define MACRO_MAX_ENTS        24     // максимум сущностей в одном файле
#elif defined(ESP8266)
#define MACRO_MAX_FILES       8      // максимум файлов-сценариев в списке
#define MACRO_MAX_ENTS        8      // максимум сущностей в одном файле
#endif
#define MACRO_EV_QUEUE        8      // размер очереди внешних событий (term/button)

// Типы сущностей сценария
#define MACRO_ENT_CRON        0      // «момент времени» по cron-выражению
#define MACRO_ENT_COND        1      // «условие» по фронту false->true
#define MACRO_ENT_BUTTON      2      // событие с веб-страницы / внешнего вызова (macro btn)
#define MACRO_ENT_TERM        3      // событие из терминала (macro msg)

// Внешнее событие (очередь term/button)
typedef struct {
    uint8_t type;            // MACRO_ENT_BUTTON или MACRO_ENT_TERM
    String  spec;            // спецификатор (может содержать несколько слов-параметров)
} MacroEvent;

struct MacroFile;

// Контекст разбора/исполнения Tcl-файла. Передаётся как arg в tcl_register.
typedef struct {
    struct MacroFile* file;  // файл, которому принадлежит интерпретатор
    bool parsing;            // true — идёт разбор файла (регистрация сущностей)
} PtclMacroCtx;

// Одна сущность сценария (строка «таблицы условий и моментов времени»)
typedef struct {
    uint8_t   type;          // MACRO_ENT_*
    String    spec;          // cron-выражение / Tcl-условие / токен button|term
    String    body;          // тело на Tcl
    cron_expr expr;          // разобранное cron-выражение (для MACRO_ENT_CRON)
    time_t    next;          // следующее срабатывание cron (0 — не инициализировано)
    bool      lastCond;      // предыдущее состояние условия (для MACRO_ENT_COND)
} MacroEntity;

// Файл-сценарий: метаданные (сохраняются в JSON) + runtime-состояние
typedef struct MacroFile {
    // --- метаданные (config_macros.json) ---
    String    name;          // полный путь: /macros/xxx.tcl
    uint8_t   prio;          // приоритет 0..7 (0 — высший)
    bool      run;           // включён пользователем
    uint32_t  created;       // время создания (локальное, TimeLib)

    // --- runtime-состояние (не сохраняется) ---
    bool      active;        // файл запущен и успешно разобран
    String    err;           // текст последней ошибки (пусто — ошибок нет)
    struct tcl* tcl;         // интерпретатор файла (только когда active)
    PtclMacroCtx ctx;        // контекст для команд Tcl (ctx.file указывает на этот файл)
    MacroEntity ents[MACRO_MAX_ENTS];
    uint8_t   nEnts;         // число сущностей в ents
} MacroFile;

// Структура конфига — сохраняется в config_macros.json
typedef struct {
    bool enabled;            // модуль включён
} strMacrosConfig;

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
    bool reloadAll();                        // перечитать все файлы сценариев
    bool fireToken(uint8_t type, const String& spec); // событие term/button в очередь
    uint8_t getFileCount();
    void printList();                        // вывод списка сценариев в терминал

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleList(AsyncWebServerRequest *request);
    void handleCreate(AsyncWebServerRequest *request);
    void handleDelete(AsyncWebServerRequest *request);
    void handleRename(AsyncWebServerRequest *request);
    void handleState(AsyncWebServerRequest *request);
    void handlePrio(AsyncWebServerRequest *request);
    void handleReload(AsyncWebServerRequest *request);
    void handleFire(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();
    bool saveMeta();                         // сохранить массив _files в config_macros.json

    // Логика работы со списком файлов
    int  findFile(const String& name);       // индекс в _files или -1
    bool nameOk(const String& name);         // проверка имени (без пути)
    void bumpMeta();                         // отметить изменение списка (пересборка в tick)
    void reconcileList();                    // скан /macros + слияние с метой + сохранение
    void ensureMacrosDir();                  // создать /macros и пример example.tcl
    String readFile(const String& path);     // содержимое файла в String
    bool writeFile(const String& path, const String& data);
    bool fsRenameFile(const String& oldPath, const String& newPath); // копия + удаление
    String uniqueNewName(const String& tmpl);// свободное имя (new_macros.tcl, new_macros1.tcl, ...)

    // Операции со списком (общие для HTTP и терминала)
    bool addFileEntry(const String& base, uint8_t prio, bool run); // новый файл в списке
    bool createNewFile(const String& base, String& fullPath);      // создать файл и запись
    bool deleteFileEntry(const String& base);                      // удалить файл и запись
    bool setFileRun(const String& base, bool on);                  // запустить/остановить
    bool setFilePrio(const String& base, int8_t delta);            // приоритет ± (clamp 0..7)
    bool renameFileEntry(const String& oldBase, const String& newBase);

    // Движок сценариев (вызывается только в main-loop: tick/терминал)
    bool parseScript(MacroFile& f);          // разобрать файл в таблицу сущностей
    void destroyScript(MacroFile& f);        // освободить интерпретатор
    void rebuildScripts();                   // синхронизация _files -> интерпретаторы
    String runBody(MacroFile& f, const String& body); // выполнить тело, вернуть ошибку
    void execBody(MacroFile& f, const String& body);  // выполнить тело и записать ошибку
    void drainEvents();                      // обработка очереди внешних событий
    void tickStep();                         // шаг исполнения (cron/cond) за одну секунду

protected:
    bool dumb;
#if defined(ESP32)
    fs::LittleFSFS* _fs;
#endif
#if defined(ESP8266)
    FS* _fs;
#endif

    strMacrosConfig _config;
    MacroFile _files[MACRO_MAX_FILES];
    uint8_t _fileCount;
    uint8_t _metaRev;        // счётчик изменений меты (для страницы — не используется напрямую)
    uint8_t _scriptRev;      // счётчик: при изменении tick пересобирает интерпретаторы
    uint8_t _lastScriptRev;  // последний обработанный tick-ом _scriptRev
    bool _ntpWasSynced;      // для инициализации cron после первой синхронизации NTP

    MacroEvent _evQueue[MACRO_EV_QUEUE];     // очередь внешних событий (term/button)
    uint8_t _evIn;
    uint8_t _evOut;

    friend void macroTickTask();   // 1-сек задача EERTOS
    friend void macroCmd();        // обработчик терминальной команды "macro"
};

extern CLASS_MODULE_MACROS module_macros;

// Терминальные команды модуля (регистрируются через TerminalRegisterModule)
void macroTerminalRegister();

#endif // _MODULE_MACROS_h
