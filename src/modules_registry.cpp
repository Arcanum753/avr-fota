#include "modules_registry.h"
// core_web/FSWebServerLib.h включается первым: предоставляет AsyncWebServerRequest,
// класс AsyncFSWebServer (ESPHTTPServer) и Arduino-типы для всех заголовков модулей.
#include "core_web/FSWebServerLib.h"

// Файл генерируется python/module_registry_gen.py под выбранный env.
// Сгенерировано для env: esp8266
// Не редактировать вручную. При смене env — перезапустить генератор.

#include "core_wifi/core_wifi.h"
#include "core_ntp/core_ntp.h"
#include "core_json/core_json.h"
#include "core_editor/core_editor.h"
#include "core_ota/core_ota.h"
#include "core_terminal/core_terminal.h"

// Определение глобального контекста приложения (extern из mod_context.h).
ModContext g_ctx;

void core_begin(ModContext& ctx) {
    core_json.begin(ctx);
    core_wifi.begin(ctx);
    core_ntp.begin(ctx);
    core_editor.begin(ctx);
    TerminalInit();
    core_ota.begin(ctx);
}

void modules_begin(ModContext& ctx) {
}

void dev_begin(ModContext& ctx) {
}

void core_web_Init() {
    core_wifi.web_Init();
    core_ntp.web_Init();
    core_json.web_Init();
    core_editor.web_Init();
    core_ota.web_Init();
}

void modules_web_Init() {
}

void dev_web_Init() {
}

void core_loop() {
    TerminalLoop();
    core_ota.loop();
}

void modules_loop() {
}

void dev_loop() {
}
