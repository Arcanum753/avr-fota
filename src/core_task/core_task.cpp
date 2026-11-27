#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_task.h"
#include "core_task_version.h"

// ============================================================
// Глобальные объекты
// ============================================================

CLASS_CORE_TASK core_task;

// ============================================================
// begin()
// ============================================================
void CLASS_CORE_TASK::begin(ModContext& ctx) {
    (void)ctx;
    DEBUGTASK("%s\r\n", __FUNCTION__);
    for (uint8_t i = 0; i < CORE_TASK_SLOTS; i++) {
        _slots[i].used = false;
        _slots[i].fn = nullptr;
        _slots[i].period = 0;
        _slots[i].periodic = false;
        _slots[i].name[0] = 0;
    }
    _lastDropped = EertosDroppedCount();
}

// ============================================================
// Публичный API
// ============================================================
bool CLASS_CORE_TASK::every(const char* name, TPTR fn, uint32_t period_ms, bool fire_now) {
    if (name == nullptr || fn == nullptr || period_ms == 0) { return false; }

    int idx = findSlot(name);
    if (idx < 0) { idx = allocSlot(); }
    if (idx < 0) {
        DEBUGTASK("no free task slot for %s\r\n", name);
        return false;
    }

    Slot& s = _slots[idx];
    strncpy(s.name, name, CORE_TASK_NAME_LEN - 1);
    s.name[CORE_TASK_NAME_LEN - 1] = 0;
    s.fn = fn;
    s.period = period_ms;
    s.periodic = true;
    s.used = true;

    armSlot(idx, period_ms);
    if (s.used == false) { return false; }

    if (fire_now) { fn(); }
    return true;
}

void CLASS_CORE_TASK::after(const char* name, TPTR fn, uint32_t delay_ms) {
    if (name == nullptr || fn == nullptr) { return; }

    int idx = findSlot(name);
    if (idx < 0) { idx = allocSlot(); }
    if (idx < 0) {
        DEBUGTASK("no free task slot for %s\r\n", name);
        return;
    }

    Slot& s = _slots[idx];
    strncpy(s.name, name, CORE_TASK_NAME_LEN - 1);
    s.name[CORE_TASK_NAME_LEN - 1] = 0;
    s.fn = fn;
    s.period = delay_ms;
    s.periodic = false;
    s.used = true;

    armSlot(idx, (delay_ms > 0) ? delay_ms : 1);
}
