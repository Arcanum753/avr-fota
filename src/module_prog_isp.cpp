#include "main.h"
#ifdef  PROGTYPE_ISP

#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef ESP32
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include "FSWebServerLib.h"
#include "debug.h"
#include "ntp_mod.h"

#include "module_prog_isp.h"
#include "common.h"

Class_ProgIsp progIsp(0);
Class_ProgIsp::Class_ProgIsp(uint8_t in): _in(in){ }





#endif