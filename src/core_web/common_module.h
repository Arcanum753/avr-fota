
#ifndef _CORE_WEB_COMMON_MODULE_h
#define _CORE_WEB_COMMON_MODULE_h

#include <Arduino.h>

class AsyncWebServerRequest;

// Вспомогательные функции ядра core_web (чистые, без состояния).
namespace ns_core_web {

// MIME-тип по имени файла (с учётом аргумента download)
String getContentType(String filename, AsyncWebServerRequest *request);

} // namespace ns_core_web

#endif // _CORE_WEB_COMMON_MODULE_h
