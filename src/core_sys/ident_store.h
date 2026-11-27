#ifndef IDENT_STORE_H
#define IDENT_STORE_H

#include <Arduino.h>

// Максимальная длина имени устройства и серийного номера (без учёта '\0').
#define IDENT_MAX_NAME     63
#define IDENT_MAX_SERIAL   63

// Чтение идентичности из энергонезависимого хранилища.
// Возвращает false, если хранилище пусто, повреждено или недоступно.
bool identStoreLoad(String &name, String &serial);

// Сохранение идентичности в энергонезависимое хранилище.
// Возвращает false при ошибке записи или превышении длины полей.
bool identStoreSave(const String &name, const String &serial);

// Полное стирание энергонезависимого хранилища.
bool identStoreErase(void);

#endif // IDENT_STORE_H
