#ifndef _MOD_CONTEXT_h
#define _MOD_CONTEXT_h

// Автономный include: гарантирует наличие String (Arduino) и типов FS.
#include <Arduino.h>

#include "main.h"

// Контекст инициализации, передаваемый каждому модулю/ядру/устройству
// в единый метод begin(ModContext& ctx). Заменяет вызов setFs() и спец-аргументы
// begin(hostname, password). Поля заполняются один раз в AsyncFSWebServer::begin().

#if defined(ESP32)
#include <LittleFS.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif

struct ModContext {
#if defined(ESP32)
    fs::LittleFSFS* fs;
#endif
#if defined(ESP8266)
    FS* fs;
#endif
    String hostname;
    String password;
};

// Глобальный контекст приложения — заполняется в AsyncFSWebServer::begin().
// Имя g_ctx (не ModContext), чтобы не конфликтовать с одноимённым типом.
extern ModContext g_ctx;

#endif // _MOD_CONTEXT_h
