
#if defined(ESP32)
#include <LittleFS.h>
#include <esp_task_wdt.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif
#include "version.h"
#include <Ticker.h>

#include "main.h"
#include "FSWebServerLib.h"
#include "eertos.h"

#include "modules_registry.h"

#include "version.h"
#include "core_led/core_led.h"

// pin used for entering setup mode
bool fsMounted = false;
Ticker _secondEERtos;

void setup() {
  
#if defined(ESP32)
    // Увеличиваем таймаут Task Watchdog до 30 секунд для обработки
    // длительных операций LittleFS и HTTP-запросов в async_tcp,
    // особенно при загрузке страниц с множеством статических ресурсов
    esp_task_wdt_init(30, true);
#endif
#if defined(ESP8266)
    ESP.wdtFeed();
#endif

    Serial.begin(115200);
    InitRTOS(); // init eertos
    fsMounted = LittleFS.begin();
    if (fsMounted == false) { DBG_MOD("[SYS] ", "\n\r\nLittleFS Mount Failed\n\r\n\r"); }
    printGitInfo();
	// WiFi is started inside library
    ESPHTTPServer.begin(&LittleFS);
    ledInit(); 
    // flashLED(CONNECTION_LED, 25, 150);
    ledMacroTimerTask();
	_secondEERtos.attach_ms(1, &TimerService); // init eertos time manager and start.
}

void loop() {
#if defined(ESP32)
    esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    // yield() обрабатывает AsyncTCP колбэки и сбрасывает watchdog
    // БЕЗ yield() ESPAsyncWebServer не может корректно обрабатывать
    // входящие HTTP-соединения, что приводит к Panic __yield
    // при рекурсивном вызове yield() внутри beginResponse()
    yield();
#endif
    // Сбрасываем watchdog перед выполнением задач EERTOS
    // TaskManager может выполнять длительные операции (LittleFS, WiFi)
    TaskManager();
    
    // Сбрасываем watchdog после TaskManager
#if defined(ESP32)
    esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
    core_loop();
    modules_loop();
    dev_loop();
}

void printGitInfo() {
    DBG_MOD("[SYS] ", "\r\n");
    #if defined(ESP32)
    DBG_MOD("[SYS] ", "*** ESP32 FIRMWARE INFORMATION ***\r\n");
	#endif
    #if defined(ESP8266)
    DBG_MOD("[SYS] ", "*** ESP8266 FIRMWARE INFORMATION ***\r\n");
	#endif
    DBG_MOD("[SYS] ", "Envoirement: %s\r\n", String(BUILD_ENV).c_str());
    
    DBG_MOD("[SYS] ", "Chip firmware ver: %s\r\n", String(FIRMWARE_VERSION).c_str());
    DBG_MOD("[SYS] ", "File system ver: %s\r\n", ESPHTTPServer.getFsVersionStr().c_str());
    DBG_MOD("[SYS] ", "Reset reason: %s\r\n", ESPHTTPServer.getResetReason().c_str());
    
    DBG_MOD("[SYS] ", "Git Branch: %s\r\n", String(GIT_BRANCH).c_str());
    DBG_MOD("[SYS] ", "Git Commit: %s\r\n", String(GIT_COMMIT).c_str());

    
    #if defined(ESP32)

	#endif  
    
}


bool isFsMounted() { return fsMounted;  }