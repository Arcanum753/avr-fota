#ifndef _COMPONENTS_INFO_h
#define _COMPONENTS_INFO_h

// ============================================================
// Метаданные скомпилированных компонентов (core/module/submodule/device).
// Сильная реализация modulesCompiledInfo() генерируется в modules_registry.cpp
// скриптом python/module_registry_gen.py под выбранный env.
// Имя файла без префикса module_ — чтобы AC-19 не принимал include за вызов модуля.
// ============================================================

#include <Arduino.h>
#include <stdint.h>

struct CompiledModuleInfo {
    const char* name;
    const char* kind;        // "core" | "module" | "submodule" | "device"
    const char* version;
    const char* commitDate;
    const char* commitHash;
    const char* ns;
    bool        web;
    bool        loop;
    bool        res;
    bool        timeSource;
    int16_t     prio;
};

// Возвращает массив компонентов и число записей. nullptr/count=0 — данных нет.
const CompiledModuleInfo* modulesCompiledInfo(uint8_t& count);

#endif // _COMPONENTS_INFO_h
