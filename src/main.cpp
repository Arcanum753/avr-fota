
#if defined(ESP32)
#include <SPIFFS.h>
#include "esp_task_wdt.h"
#endif
#if defined(ESP8266)
#include <FS.h>
#endif
#include "version.h"
#include <Ticker.h>

#include "main.h"
#include "FSWebServerLib.h"
#include "eertos.h"

#include "core_ota/module_ota.h"
#include "core_terminal/ErriezSerialTerminal.h"
#include "core_terminal/module_terminal.h"

#include "version.h"
#include "common_gpio.h"

// pin used for entering setup mode
bool fsMounted = false;
Ticker _secondEERtos;

void setup() {
  
    Serial.begin(115200);
    InitRTOS(); // init eertos
    fsMounted = SPIFFS.begin();
    if (fsMounted == false) { Serial.println("\n\r\nSPIFFS Mount Failed\n\r\n\r"); }
    printGitInfo();
	// WiFi is started inside library
    ESPHTTPServer.begin(&SPIFFS);
    TerminalInit();
    flashLEDinit(); 
    flashLED(CONNECTION_LED, 25, 150);
	_secondEERtos.attach_ms(1, &TimerService); // init eertos time manager and start.
}

void loop() {
#if defined(ESP32)
    esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    ESP.wdtFeed(); 
#endif
    TaskManager();
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
    Serial.println("Build Date and time: " + String(BUILD_TIME));
    
    Serial.println("Git Branch: " + String(GIT_BRANCH));
    Serial.println("Git Commit: " + String(GIT_COMMIT));

    
    #if defined(ESP32)
    Serial.println(" ESP32 WebPages version: "+ String(VERSION_WEB));
	#endif
    #if defined(ESP8266)
    Serial.println(" ESP8266 WebPages version: "+ String(VERSION_WEB));
	#endif  
    
}


bool isFsMounted() { return fsMounted;  }