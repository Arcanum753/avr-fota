#ifndef _CORE_TASK_H
#define _CORE_TASK_H

#include "main.h"

#include <Arduino.h>

#include "mod_context.h"
#include "core_sys/eertos.h"
#include "core_task_types.h"

#if defined(DEBUG_TASK)
#define DEBUGTASK(...) DBG_MOD("[C_TASK] ", __VA_ARGS__)
#endif
#if !defined(DEBUG_TASK)
#define DEBUGTASK(...)
#endif

// ============================================================
// Именованные периодические/отложенные задачи поверх EERTOS.
// Не создаёт второй планировщик: каждый слот — своя статическая
// trampoline-функция (SetTimerTask идемпотентен по указателю).
// ============================================================
class CLASS_CORE_TASK {
public:
    void begin(ModContext& ctx);
    void loop();

    bool every(const char* name, TPTR fn, uint32_t period_ms, bool fire_now = false);
    void after(const char* name, TPTR fn, uint32_t delay_ms);
    void cancel(const char* name);
    void cancelAll();

    // Вызывается статическими trampoline-функциями.
    void runIdx(uint8_t idx);

private:
    struct Slot {
        char     name[CORE_TASK_NAME_LEN] = { 0 };
        TPTR     fn       = nullptr;
        uint32_t period   = 0;
        bool     used     = false;
        bool     periodic = false;
    };

    int  findSlot(const char* name);
    int  allocSlot();
    void armSlot(int idx, uint32_t delayMs);

protected:
    Slot        _slots[CORE_TASK_SLOTS];
    uint32_t    _lastDropped = 0;
};

extern CLASS_CORE_TASK core_task;

#endif // _CORE_TASK_H
