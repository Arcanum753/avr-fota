#ifndef _CORE_STATE_ENGINE_h
#define _CORE_STATE_ENGINE_h

// ============================================================
// core_state_engine.h — интерфейс исполнительной части core_state,
// используемый шаблонным core_state.cpp.
// Реализация: core_state_engine.cpp
// ============================================================

#include "core_state_types.h"

// Текстовое представление значения (Bool/I32/F32/TIME/ENUM/STR).
String coreStateBusText(const BusValue& v);

#endif // _CORE_STATE_ENGINE_h
