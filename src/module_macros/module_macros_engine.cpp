// ============================================================
// module_macros_engine.cpp — исполнительная машина сценариев
// (pTcl-интерпретатор + cron + таблица сущностей)
// ============================================================

#include "module_macros.h"

#include "core_ntp/NtpClientLib.h"
#include "common/TimeLib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Форвард-объявления, необходимые до определений ниже
static int macroEvalChunks(struct tcl* t, const String& src, String* errOut);

// Вспомогательные функции движка определены в extern "C"-блоке ниже
extern "C" {
static String tclValueToString(tcl_value_t* v);
static void macroTclRegisterExtras(struct tcl* t, void* ctx);
}

// ============================================================
// Движок сценариев
// ============================================================

// Обёртка для tcl.c: исполнение скрипта в интерпретаторе через проверенный
// C++-вариант построчной разбивки (используется для тел пользовательских proc)
/** Выполняет Tcl-скрипт по командам верхнего уровня (используется tcl.c для тел proc).
 \param t интерпретатор pTcl
 \param src null-терминированный текст сценария
 \return FNORMAL при успехе либо код потока управления/FERROR
 */

extern "C" int macroTclEvalScript(struct tcl* t, const char* src) {
    if (t == NULL || src == NULL) { return FERROR; }
    String s(src);
    return macroEvalChunks(t, s, NULL);
}

// Выполнение Tcl-кода по командам: pTcl надёжно обрабатывает несколько команд
// только при поочерёдном вызове, поэтому скрипт режем на команды уровня 0
// (с учётом {} , [] , "" и комментариев #) и исполняем каждую отдельно
// в том же интерпретаторе (процедуры/переменные сохраняются между вызовами).
/** Проверяет, что строка состоит только из пробелов/табуляций (начало команды перед комментарием).
 \param s проверяемая строка
 \return true — только пробелы
 */
static bool macroWsOnly(const String& s) {
    for (uint8_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c != ' ' && c != '\t') { return false; }
    }
    return true;
}

/** Исполняет Tcl-код по отдельным командам: режет скрипт уровня 0
 (учитывая {} [] "" и комментарии #) и вызывает tcl_eval для каждой команды.
 Процедуры и переменные интерпретатора сохраняются между вызовами.
 \param t интерпретатор
 \param src текст сценария
 \param errOut текст ошибки (при FERROR)
 \return FERROR при ошибке, иначе FNORMAL
 */
static int macroEvalChunks(struct tcl* t, const String& src, String* errOut) {
    String chunk;
    int brace = 0;
    int bracket = 0;
    bool inQuote = false;
    bool comment = false;
    size_t len = src.length();

    for (size_t i = 0; i < len; i++) {
        char c = src.charAt((unsigned int)i);

        if (comment) {
            if (c == '\n' || c == '\r') { comment = false; }
            continue;
        }
        // Комментарий до конца строки (только в начале команды)
        if (c == '#' && brace == 0 && bracket == 0 && !inQuote && macroWsOnly(chunk)) {
            comment = true;
            continue;
        }
        // Разделитель команд (вне {} [] "")
        if (!inQuote && brace == 0 && bracket == 0 && (c == '\n' || c == ';')) {
            chunk.trim();
            if (chunk.length() > 0) {
                // Ошибкой считаем только FERROR: proc-определения на верхнем
                // уровне возвращают FRETURN - это не ошибка разбора.
                int r = tcl_eval(t, chunk.c_str(), chunk.length() + 1);
                if (r == FERROR) {
                    if (errOut != NULL && errOut->length() == 0 && t->result &&
                        tcl_length(t->result) > 0) {
                        *errOut = tclValueToString(t->result);
                    }
                    return r;
                }
            }
            chunk = "";
            continue;
        }

        if (!inQuote) {
            if (c == '{') { brace++; }
            else if (c == '}') { if (brace > 0) { brace--; } }
            else if (c == '[') { bracket++; }
            else if (c == ']') { if (bracket > 0) { bracket--; } }
            else if (c == '"') { inQuote = true; }
        } else if (c == '"') {
            inQuote = false;
        }
        chunk += c;
    }

    // Последняя команда
    chunk.trim();
    if (chunk.length() > 0) {
        int r = tcl_eval(t, chunk.c_str(), chunk.length() + 1);
        if (r == FERROR) {
            if (errOut != NULL && errOut->length() == 0 && t->result &&
                tcl_length(t->result) > 0) {
                *errOut = tclValueToString(t->result);
            }
            return r;
        }
    }
    return FNORMAL;
}

/** Освобождает интерпретатор файла и очищает таблицу его сущностей.
 \param f сценарий
 */
void CLASS_MODULE_MACROS::destroyScript(MacroFile& f) {
    if (f.tcl) {
        tcl_destroy(f.tcl);
        free(f.tcl);
        f.tcl = NULL;
    }
    for (uint8_t i = 0; i < f.nEnts; i++) {
        f.ents[i].spec = "";
        f.ents[i].body = "";
    }
    f.nEnts = 0;
    f.active = false;
    f.ctx.parsing = false;
}

/** Разбирает файл сценария: читает содержимое из FS, создаёт интерпретатор,
 регистрирует команды, исполняет файл по командам и заполняет таблицу сущностей.
 \param f сценарий (runtime-часть)
 \return true — разбор успешен и есть хотя бы одно правило
 */
bool CLASS_MODULE_MACROS::parseScript(MacroFile& f) {
    f.err = "";
    destroyScript(f);

    String content = readFile(f.name);
    if (content.length() == 0) {
        f.err = "File is empty or unreadable";
        DEBUGMACROS("%s: %s\r\n", __FUNCTION__, f.err.c_str());
        return false;
    }

    struct tcl* t = (struct tcl*)calloc(1, sizeof(struct tcl));
    if (t == NULL) {
        f.err = "No memory for interpreter";
        return false;
    }
    tcl_init(t);
    f.tcl = t;
    f.ctx.file = &f;

    macroTclRegisterExtras(t, (void*)&f.ctx);

    f.ctx.parsing = true;
    int r = macroEvalChunks(t, content, &f.err);
    f.ctx.parsing = false;

    if (r == FERROR) {
        if (f.err.length() == 0) {
            f.err = "Scenario parse error";
        }
        DEBUGMACROS("%s: %s -> %s\r\n", __FUNCTION__, f.name.c_str(), f.err.c_str());
        destroyScript(f);
        return false;
    }

    if (f.nEnts == 0) {
        f.err = "No rules registered in the scenario";
        DEBUGMACROS("%s: %s -> %s\r\n", __FUNCTION__, f.name.c_str(), f.err.c_str());
        destroyScript(f);
        return false;
    }

    f.active = true;
    DEBUGMACROS("%s: %s -> %d ent\r\n", __FUNCTION__, f.name.c_str(), f.nEnts);
    return true;
}

/** Синхронизирует интерпретаторы с метой: останавливает удалённые/выключенные
 файлы и переразбирает запущенные.
 */
void CLASS_MODULE_MACROS::rebuildScripts() {
    for (uint8_t i = 0; i < _fileCount; i++) {
        MacroFile& f = _files[i];
        if (f.run == false) {
            destroyScript(f);
            continue;
        }
        if (_fs->exists(f.name) == false) {
            f.err = "File is missing";
            destroyScript(f);
            continue;
        }
        // Полная пересборка: разрушаем и разбираем заново
        parseScript(f);
    }
}

/** Выполняет тело правила в интерпретаторе файла.
 \param f сценарий
 \param body текст тела (Tcl)
 \return пустая строка при успехе, иначе текст ошибки
 */
String CLASS_MODULE_MACROS::runBody(MacroFile& f, const String& body) {
    if (f.tcl == NULL || f.active == false) { return "No interpreter"; }
    String errTxt;
    int r = macroEvalChunks(f.tcl, body, &errTxt);
    if (r == FERROR) {
        if (errTxt.length() > 0) { return errTxt; }
        return "Scenario execution error";
    }
    return "";
}

// Выполнение тела с записью ошибки в файл (метод класса: доступ к runBody)
/** Выполняет тело правила и записывает ошибку в f.err (с выводом в журнал).
 \param f сценарий
 \param body текст тела
 */
void CLASS_MODULE_MACROS::execBody(MacroFile& f, const String& body) {
    String err = runBody(f, body);
    if (err.length() > 0) {
        f.err = err;
        DEBUGMACROS("[MACRO] %s exec error: %s\r\n", f.name.c_str(), err.c_str());
    }
}

// Вычисление Tcl-условия; возвращает истину/ложь, при ошибке пишет текст в errOut
/** Вычисляет Tcl-условие правила cond.
 \param f сценарий
 \param cond текст условия
 \param errOut текст ошибки
 \return истинность условия (0 = ложь)
 */
static bool macroEvalCond(MacroFile& f, const String& cond, String& errOut) {
    if (f.tcl == NULL || f.active == false) {
        errOut = "No interpreter";
        return false;
    }
    int r = macroEvalChunks(f.tcl, cond, &errOut);
    if (r == FERROR) {
        if (errOut.length() == 0) { errOut = "Condition evaluation error"; }
        return false;
    }
    return (tcl_int(f.tcl->result) != 0);
}

/** Обрабатывает очередь внешних событий: сопоставляет term/button-правила
 запущенных файлов и выполняет их тела.
 */
void CLASS_MODULE_MACROS::drainEvents() {
    while (_evIn != _evOut) {
        MacroEvent ev = _evQueue[_evOut];
        _evOut = (_evOut + 1) % MACRO_EV_QUEUE;

        for (uint8_t i = 0; i < _fileCount; i++) {
            MacroFile& f = _files[i];
            if (f.run == false || f.active == false) { continue; }
            for (uint8_t j = 0; j < f.nEnts; j++) {
                MacroEntity& e = f.ents[j];
                if (e.type == ev.type && e.spec == ev.spec) {
                    const char* tn = (ev.type == MACRO_ENT_TERM) ? "term" : "button";
                    Serial.printf("[MACRO] condition %s \"%s\" fired\r\n", tn, ev.spec.c_str());
                    execBody(f, e.body);
                }
            }
        }
    }
}

/** Один шаг исполнительной машины (раз в секунду): при изменении списка
 пересобирает сценарии, затем обрабатывает события и исполняет cron/cond-правила.
 */
void CLASS_MODULE_MACROS::tickStep() {
    // Пересборка интерпретаторов при изменении списка
    if (_scriptRev != _lastScriptRev) {
        _lastScriptRev = _scriptRev;
        rebuildScripts();
    }

    if (_config.enabled == false) { return; }

    // Внешние события (term/button)
    drainEvents();

    bool ntpSync = (NTP.getLastNTPSync() > 0);
    if (ntpSync && !_ntpWasSynced) {
        _ntpWasSynced = true;
        DEBUGMACROS("[MACRO] NTP synced, cron activated\r\n");

        // Файлы, добавленные до синхронизации времени (created == 0),
        // получают реальную дату создания
        bool changed = false;
        for (uint8_t i = 0; i < _fileCount; i++) {
            if (_files[i].created == 0) {
                _files[i].created = (uint32_t)now();
                changed = true;
            }
        }
        if (changed) { saveConfig(); }
    }

    // Проход по запущенным файлам и их сущностям
    for (uint8_t i = 0; i < _fileCount; i++) {
        MacroFile& f = _files[i];
        if (f.run == false || f.active == false) { continue; }

        for (uint8_t j = 0; j < f.nEnts; j++) {
            MacroEntity& e = f.ents[j];

            if (e.type == MACRO_ENT_CRON) {
                if (ntpSync == false) { continue; }

                if (e.next == 0) {
                    // Инициализация следующего момента после синхронизации времени
                    time_t nx = cron_next(&e.expr, (time_t)now());
                    e.next = nx;
                    continue;
                }
                if (e.next == (time_t)-1) { continue; } // расписание не имеет ближайших моментов

                if ((time_t)now() >= e.next) {
                    DEBUGMACROS("[MACRO] %s cron(%s)\r\n", f.name.c_str(), e.spec.c_str());
                    execBody(f, e.body);
                    e.next = cron_next(&e.expr, (time_t)now());
                }
            }

            if (e.type == MACRO_ENT_COND) {
                String errTxt;
                bool truth = macroEvalCond(f, e.spec, errTxt);
                if (errTxt.length() > 0) {
                    f.err = errTxt;
                    DEBUGMACROS("[MACRO] %s cond error: %s\r\n", f.name.c_str(), errTxt.c_str());
                    continue;
                }
                if (truth && e.lastCond == false) {
                    // Фронт false -> true
                    DEBUGMACROS("[MACRO] %s cond(true)\r\n", f.name.c_str());
                    e.lastCond = true;
                    execBody(f, e.body);
                } else if (truth == false) {
                    e.lastCond = false;
                }
            }
        }
    }
}



// ============================================================
// Команды Tcl, регистрируемые в интерпретаторах сценариев
// ============================================================

// Записать в result текст ошибки и вернуть FERROR
/** Записывает текст ошибки в результат интерпретатора и возвращает FERROR.
 */
static int macroTclResultError(struct tcl* t, const char* msg) {
    return tcl_result(t, FERROR, tcl_alloc(msg, strlen(msg)));
}

/** То же, что macroTclResultError, но принимает Arduino String.
 */
static int macroTclResultErrorS(struct tcl* t, const String& msg) {
    if (msg.length() == 0) { return macroTclResultError(t, "error"); }
    return tcl_result(t, FERROR, tcl_alloc(msg.c_str(), msg.length()));
}

// Прочитать слово аргумента списком в String
/** Читает аргумент команды Tcl из списка и копирует его в Arduino String.
 \param t интерпретатор
 \param args список аргументов
 \param idx индекс аргумента
 \param ok true, если аргумент получен
 \return строка аргумента
 */
static String macroReadArg(struct tcl* t, tcl_value_t* args, int idx, bool* ok) {
    *ok = false;
    tcl_value_t* v = tcl_list_at(args, idx);
    if (v == NULL) { return ""; }
    String s = tclValueToString(v);
    tcl_free(v);
    *ok = true;
    return s;
}

// Копия значения Tcl в Arduino String (переносимо между ESP32/ESP8266)
/** Копирует значение Tcl в Arduino String (переносимо между ESP32/ESP8266).
 \param v значение pTcl (или NULL)
 \return строка
 */
static String tclValueToString(tcl_value_t* v) {
    String out;
    if (v == NULL) { return out; }
    int len = tcl_length(v);
    const char* s = tcl_string(v);
    out.reserve((unsigned int)len);
    for (int i = 0; i < len; i++) { out += s[i]; }
    return out;
}

// Регистрация сущности: cron / cond / button / term {spec} {body}
extern "C" {
/** Команда Tcl cron/cond/button/term: регистрирует правило в таблице сценария.
 Допустима только во время разбора файла.
 \param t интерпретатор
 \param args список аргументов (команда, спецификатор, тело)
 \param arg контекст PtclMacroCtx
 \return код потока управления
 */
static int tclCmdEntity(struct tcl* t, tcl_value_t* args, void* arg) {
    PtclMacroCtx* pc = (PtclMacroCtx*)arg;
    if (pc == NULL || pc->file == NULL) { return macroTclResultError(t, "no scenario context"); }
    if (pc->parsing == false) { return macroTclResultError(t, "rules can be registered only during file parse"); }

    MacroFile* f = pc->file;
    if (tcl_list_length(args) != 3) { return macroTclResultError(t, "usage: <cmd> {specifier} {body}"); }

    tcl_value_t* namev = tcl_list_at(args, 0);
    const char* cmdName = namev ? tcl_string(namev) : "";
    DEBUGMACROS("[MACRO] reg: %s\r\n", cmdName);
    uint8_t type;
    if (strcmp(cmdName, "cron") == 0)        { type = MACRO_ENT_CRON; }
    else if (strcmp(cmdName, "cond") == 0)   { type = MACRO_ENT_COND; }
    else if (strcmp(cmdName, "button") == 0) { type = MACRO_ENT_BUTTON; }
    else if (strcmp(cmdName, "term") == 0)   { type = MACRO_ENT_TERM; }
    else {
        if (namev) { tcl_free(namev); }
        return macroTclResultError(t, "unknown registration command");
    }
    if (namev) { tcl_free(namev); }

    if (f->nEnts >= MACRO_MAX_ENTS) { return macroTclResultError(t, "file rule limit exceeded"); }

    bool ok1, ok2;
    String spec = macroReadArg(t, args, 1, &ok1);
    String body = macroReadArg(t, args, 2, &ok2);
    if (ok1 == false || ok2 == false) { return macroTclResultError(t, "not enough arguments"); }
    spec.trim();
    if (spec.length() == 0) { return macroTclResultError(t, "empty specifier"); }
    if (body.length() == 0) { return macroTclResultError(t, "empty rule body"); }

    MacroEntity& e = f->ents[f->nEnts];
    e.type = type;
    e.spec = spec;
    e.body = body;
    e.lastCond = false;
    e.next = 0;

    if (type == MACRO_ENT_CRON) {
        const char* perr = NULL;
        cron_expr cx;
        memset(&cx, 0, sizeof(cx));
        cron_parse_expr(spec.c_str(), &cx, &perr);
        if (perr) {
            String msg = "cron expression error: ";
            msg += perr;
            msg += " (";
            msg += spec;
            msg += ")";
            return macroTclResultErrorS(t, msg);
        }
        e.expr = cx;
    }

    f->nEnts++;
    return tcl_result(t, FNORMAL, tcl_alloc("", 0));
}

// puts — вывод в последовательный порт
/** Команда Tcl puts: выводит текст в терминал с префиксом [MACRO].
 */
static int tclCmdPuts(struct tcl* t, tcl_value_t* args, void* arg) {
    (void)arg;
    if (tcl_list_length(args) < 2) { return tcl_result(t, FNORMAL, tcl_alloc("", 0)); }

    tcl_value_t* text = tcl_list_at(args, 1);
    if (text == NULL) { return tcl_result(t, FNORMAL, tcl_alloc("", 0)); }

    const char* s = tcl_string(text);
    int len = tcl_length(text);
    Serial.printf("[MACRO] ");
    for (int i = 0; i < len; i++) { Serial.write((uint8_t)s[i]); }
    Serial.printf("\r\n");

    int r = tcl_result(t, FNORMAL, tcl_dup(text));
    tcl_free(text);
    return r;
}

// now — текущие секунды (локальное «наивное» время TimeLib)
/** Команда Tcl now: возвращает текущие секунды (локальное «наивное» время TimeLib).
 */
static int tclCmdNow(struct tcl* t, tcl_value_t* args, void* arg) {
    (void)args;
    (void)arg;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)now());
    return tcl_result(t, FNORMAL, tcl_alloc(buf, strlen(buf)));
}

// clock — текущее время в формате ЧЧ:ММ:СС (для печати в примерах)
/** Команда Tcl clock: возвращает текущее время в формате ЧЧ:ММ:СС.
 */
static int tclCmdClock(struct tcl* t, tcl_value_t* args, void* arg) {
    (void)args;
    (void)arg;
    time_t tnow = (time_t)now();
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             (int)hour(tnow), (int)minute(tnow), (int)second(tnow));
    return tcl_result(t, FNORMAL, tcl_alloc(buf, strlen(buf)));
}

/** Регистрирует в интерпретаторе команды модуля (cron/cond/button/term/puts/now/clock).
 \param t интерпретатор
 \param ctx контекст файла для команд регистрации
 */
static void macroTclRegisterExtras(struct tcl* t, void* ctx) {
    tcl_register(t, "cron",   tclCmdEntity, 0, ctx);
    tcl_register(t, "cond",   tclCmdEntity, 0, ctx);
    tcl_register(t, "button", tclCmdEntity, 0, ctx);
    tcl_register(t, "term",   tclCmdEntity, 0, ctx);
    tcl_register(t, "puts",   tclCmdPuts, 0, ctx);
    tcl_register(t, "now",    tclCmdNow, 0, ctx);
    tcl_register(t, "clock",  tclCmdClock, 0, ctx);
}
} // extern "C"

