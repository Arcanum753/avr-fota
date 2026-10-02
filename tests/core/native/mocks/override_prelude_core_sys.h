#ifndef _MOCK_OVERRIDE_PRELUDE_CORE_SYS_H
#define _MOCK_OVERRIDE_PRELUDE_CORE_SYS_H

// Prelude для host-тестов подсистемы времени core_sys. В отличие от
// override_prelude.h НЕ подменяет core_sys.h — тестируется настоящий класс
// CLASS_CORE_SYS (core_sys_time.cpp). Перекрываются только core_web и version.

#ifdef __cplusplus

#include "Arduino.h"
#include "ESPAsyncWebServer.h"

// guard'ы реальных файлов (см. src/...)
#define _ESPAsyncWebServer_H_   // src/ESPAsyncWebServer.h
#define _FSWEBSERVERLIB_h       // core_web/FSWebServerLib.h
#define VERSION_H               // src/version.h

#include "core_web/FSWebServerLib.h"  // мок
#include "version.h"                  // мок (фиксированные версии)

#endif // __cplusplus
#endif // _MOCK_OVERRIDE_PRELUDE_CORE_SYS_H
