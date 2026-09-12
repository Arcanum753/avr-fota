#ifndef _CORE_OTA_LED_h
#define _CORE_OTA_LED_h

// ============================================================
// core_ota_led.h — кассета светодиодной индикации обновления
// (общая для core_ota и module_otaclient).
// Реализация: core_ota_engine.cpp
// ============================================================

void ledMacrosUpdateFirmware();
void ledMacrosUpdateFilesystem();
void ledMacrosUpdateError();

#endif // _CORE_OTA_LED_h
