

#include "main.h"
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif


#include <ArduinoJson.h>
#include "FSWebServerLib.h"
#include "debug.h"
#include "module_ota.h"



MODULE_OTA_CLASS :: MODULE_OTA_CLASS (bool _in) {
	 dumb = _in;
 }