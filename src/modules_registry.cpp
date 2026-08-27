#include "modules_registry.h"
// FSWebServerLib.h включается первым: предоставляет AsyncWebServerRequest,
// класс AsyncFSWebServer (ESPHTTPServer) и Arduino-типы для всех заголовков модулей.
#include "FSWebServerLib.h"

// Файл генерируется python/module_registry_gen.py под выбранный env.
// Сгенерировано для env: esp32_clock-mech_ring
// Не редактировать вручную. При смене env — перезапустить генератор.

#include "core_wifi/core_wifi.h"
#include "core_ntp/core_ntp.h"
#include "core_json/core_json.h"
#include "core_editor/core_editor.h"
#include "core_ota/core_ota.h"
#include "core_terminal/core_terminal.h"
#include "device_clock-mech/device_clock-mech.h"
#include "device_mech-ring/device_mech-ring.h"
#include "module_ds3231/module_ds3231.h"
#include "module_i2c-mapper/module_i2c-mapper.h"
#include "module_otaclient/module_otaclient.h"
#include "module_udp/module_udp.h"

// Определение глобального контекста приложения (extern из mod_context.h).
ModContext g_ctx;

void core_begin(ModContext& ctx) {
    ModClassJson.begin(ctx);
    modWifiClass.begin(ctx);
    modNtpClass.begin(ctx);
    ModClassEdit.begin(ctx);
    TerminalInit();
    otaClient.begin(ctx);
}

void modules_begin(ModContext& ctx) {
    ModClassDs3231.begin(ctx);
    ModClassI2cMapper.begin(ctx);
}

void dev_begin(ModContext& ctx) {
    ModClassClockMech.begin(ctx);
    ModClassRingMech.begin(ctx);
}

void core_web_Init() {
    modWifiClass.web_Init();
    modNtpClass.web_Init();
    ModClassJson.web_Init();
    ModClassEdit.web_Init();
    otaClient.web_Init();
}

void modules_web_Init() {
    ModClassDs3231.web_Init();
    ModClassI2cMapper.web_Init();
    udpBroadcast.web_Init();
}

void dev_web_Init() {
    ModClassClockMech.web_Init();
    ModClassRingMech.web_Init();
}

void core_loop() {
    TerminalLoop();
    otaClient.loop();
}

void modules_loop() {
}

void dev_loop() {
}
