#ifndef _MOCK_OVERRIDE_PRELUDE_H
#define _MOCK_OVERRIDE_PRELUDE_H

// Принудительно подключается через build_flags: -include ../native/mocks/override_prelude.h
// Определяет include-guard'ы реальных заголовков ядра ДО их включения, чтобы
// вместо тяжёлых/несовместимых с хостом использовались мок-версии.
// Обязательно C-safe: Unity (.c) компилируется теми же build_flags.

#ifdef __cplusplus

#include "Arduino.h"
#include "ESPAsyncWebServer.h"

// guard'ы реальных файлов (см. src/...)
#define _ESPAsyncWebServer_H_   // src/ESPAsyncWebServer.h
#define _FSWEBSERVERLIB_h       // core_web/FSWebServerLib.h
#define _CORE_SYS_H             // core_sys/core_sys.h
#define _MODULENTP_h            // core_ntp/core_ntp.h
#define VERSION_H               // src/version.h

#include "core_web/FSWebServerLib.h"  // мок
#include "core_sys/core_sys.h"        // мок
#include "core_ntp/core_ntp.h"        // мок
#include "version.h"                  // мок (фиксированные версии)

#endif // __cplusplus
#endif // _MOCK_OVERRIDE_PRELUDE_H
