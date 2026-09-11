#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_task.h"
#include "core_task_version.h"

// ============================================================
// Глобальные объекты
// ============================================================

CLASS_CORE_TASK core_task;

// Статические trampoline-функции: по одной на слот, чтобы SetTimerTask
// (идемпотентный по указателю) не путал разные именованные задачи.
#define CORE_TASK_THUNK(n) static void coreTaskThunk##n(void) { core_task.runIdx(n); }
CORE_TASK_THUNK(0)
CORE_TASK_THUNK(1)
CORE_TASK_THUNK(2)
CORE_TASK_THUNK(3)
CORE_TASK_THUNK(4)
CORE_TASK_THUNK(5)
CORE_TASK_THUNK(6)
CORE_TASK_THUNK(7)
CORE_TASK_THUNK(8)
CORE_TASK_THUNK(9)
CORE_TASK_THUNK(10)
CORE_TASK_THUNK(11)

static TPTR const coreTaskThunks[CORE_TASK_SLOTS] = {
    coreTaskThunk0,  coreTaskThunk1,  coreTaskThunk2,  coreTaskThunk3,
    coreTaskThunk4,  coreTaskThunk5,  coreTaskThunk6,  coreTaskThunk7,
    coreTaskThunk8,  coreTaskThunk9,  coreTaskThunk10, coreTaskThunk11,
};

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
// Слоты
// ============================================================
int CLASS_CORE_TASK::findSlot(const char* name) {
    if (name == nullptr) { return -1; }
    for (uint8_t i = 0; i < CORE_TASK_SLOTS; i++) {
        if (_slots[i].used && strcmp(_slots[i].name, name) == 0) { return i; }
    }
    return -1;
}

int CLASS_CORE_TASK::allocSlot() {
    for (uint8_t i = 0; i < CORE_TASK_SLOTS; i++) {
        if (!_slots[i].used) { return i; }
    }
    return -1;
}

void CLASS_CORE_TASK::armSlot(int idx, uint32_t delayMs) {
    if (idx < 0 || idx >= CORE_TASK_SLOTS) { return; }
    if (SetTimerTaskEx(coreTaskThunks[idx], delayMs) == false) {
        DEBUGTASK("timer slot overflow for %s\r\n", _slots[idx].name);
        _slots[idx].used = false;
        _slots[idx].fn = nullptr;
    }
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

void CLASS_CORE_TASK::cancel(const char* name) {
    int idx = findSlot(name);
    if (idx < 0) { return; }
    DelTimerTask(coreTaskThunks[idx]);
    _slots[idx].used = false;
    _slots[idx].fn = nullptr;
}

void CLASS_CORE_TASK::cancelAll() {
    for (uint8_t i = 0; i < CORE_TASK_SLOTS; i++) {
        if (_slots[i].used) {
            DelTimerTask(coreTaskThunks[i]);
            _slots[i].used = false;
            _slots[i].fn = nullptr;
        }
    }
}

// ============================================================
// Исполнение слота
// ============================================================
void CLASS_CORE_TASK::runIdx(uint8_t idx) {
    if (idx >= CORE_TASK_SLOTS) { return; }
    Slot& s = _slots[idx];
    if (!s.used || s.fn == nullptr) { return; }

    TPTR fn = s.fn;
    uint32_t period = s.period;
    bool periodic = s.periodic;

    fn();

    // Задача могла отменить или перерегистрировать свой слот.
    if (!_slots[idx].used || _slots[idx].fn != fn) { return; }

    if (periodic) {
        armSlot(idx, period);
    } else {
        _slots[idx].used = false;
        _slots[idx].fn = nullptr;
    }
}

// ============================================================
// loop() — только диагностика (TaskManager зовётся из main.cpp)
// ============================================================
void CLASS_CORE_TASK::loop() {
    uint32_t dropped = EertosDroppedCount();
    if (dropped != _lastDropped) {
        DEBUGTASK("EERTOS dropped tasks: %lu\r\n", (unsigned long)dropped);
        _lastDropped = dropped;
    }
}
