#include "components_info.h"

// Слабая заглушка для host-тестов и сборок без сгенерированного registry.
// Сильная реализация находится в сгенерированном src/modules_registry.cpp.
#if defined(__GNUC__)
__attribute__((weak))
#endif
const CompiledModuleInfo* modulesCompiledInfo(uint8_t& count) {
    count = 0;
    return nullptr;
}
