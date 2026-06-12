
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#endif
#if defined(ESP8266)
#include <FS.h>
#endif
#include "version.h"
#include <Ticker.h>

#include "main.h"
#include "FSWebServerLib.h"
#include "eertos.h"

#include "core_ota/core_ota.h"
#include "core_terminal/ErriezSerialTerminal.h"
#include "core_terminal/core_terminal.h"

#include "version.h"
#include "core_led/core_led.h"

// pin used for entering setup mode
bool fsMounted = false;
Ticker _secondEERtos;

void setup() {
  
#if defined(ESP32)
    // Увеличиваем таймаут Task Watchdog до 30 секунд для обработки
    // длительных операций SPIFFS и HTTP-запросов в async_tcp,
    // особенно при загрузке страниц с множеством статических ресурсов
    esp_task_wdt_init(30, true);
#endif
#if defined(ESP8266)
    ESP.wdtFeed();
#endif

    Serial.begin(115200);
    InitRTOS(); // init eertos
    fsMounted = SPIFFS.begin();
    if (fsMounted == false) { Serial.println("\n\r\nSPIFFS Mount Failed\n\r\n\r"); }
    printGitInfo();
	// WiFi is started inside library
    ESPHTTPServer.begin(&SPIFFS);
    TerminalInit();
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
    // TaskManager может выполнять длительные операции (SPIFFS, WiFi)
    TaskManager();
    
    // Сбрасываем watchdog после TaskManager
#if defined(ESP32)
    esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
    
    loop_user();
    TerminalLoop();
    modOtaClass.loopHandler();

}

void loop_user(){

}

void printGitInfo() {
    Serial.println("\n");
    #if defined(ESP32)
    Serial.println("*** ESP32 FIRMWARE INFORMATION ***");
	#endif
    #if defined(ESP8266)
    Serial.println("*** ESP8266 FIRMWARE INFORMATION ***");
	#endif
    Serial.println("Envoirement: " + String(BUILD_ENV));
    
    Serial.println("Chip firmware ver: " + String(FIRMWARE_VERSION));
    Serial.println("File system ver: " + ESPHTTPServer.getFsVersionStr());
    
    
    
    Serial.println("Git Branch: " + String(GIT_BRANCH));
    Serial.println("Git Commit: " + String(GIT_COMMIT));

    
    #if defined(ESP32)

	#endif  
    
}


bool isFsMounted() { return fsMounted;  }