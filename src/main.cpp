
#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include "version.h"
#include <ESPAsyncWebServer.h>
#include <Ticker.h>

#include "main.h"
#include "FSWebServerLib.h"
#include "eertos.h"

#include "core_terminal/ErriezSerialTerminal.h"
#include "core_terminal/module_terminal.h"
#include "core_ota/module_ota.h"




// pin used for entering setup mode

Ticker _secondEERtos;
void TaskBlink1();
void TaskBlink2();


// unsigned long previousMillis = 0;
// unsigned long interval = 10000;

void setup() {
  
    Serial.begin(115200);
    InitRTOS(); // init eertos
	_secondEERtos.attach_ms(1, &TimerService); // init eertos time manager
	// SetTask(TaskBlink1); // do blink
	SPIFFS.begin(); // Not really needed, checked inside library and started if
					// needed

    printGitInfo();
	// WiFi is started inside library
    ESPHTTPServer.begin(&SPIFFS);

    TerminalInit();
    
    
    
}

void loop() {
    TaskManager();
    loop_user();
    TerminalLoop();
    modOtaClass.loopHandler();
}

void loop_user(){

}



void TaskBlink1(){
     SetTimerTask(TaskBlink2, 3000);
}

void TaskBlink2(){
      SetTimerTask(TaskBlink1, 1000);
}

void ledInit(){
    
    // pinMode(PIN_MISO, OUTPUT);
    // pinMode(PIN_MOSI, OUTPUT);
    // pinMode(PIN_SCK, OUTPUT);
    // pinMode(PIN_RST, OUTPUT);

}




void printGitInfo() {
    Serial.println("\n");
    #if defined(ESP32)
    Serial.println("*** ESP32 FIRMWARE INFORMATION ***");
	#endif
    #if defined(ESP8266)
    Serial.println(" ESP8266 FIRMWARE INFORMATION");
	#endif
    
    Serial.println("Git Branch: " + String(GIT_BRANCH));
    Serial.println("Git Commit: " + String(GIT_COMMIT));
    
    Serial.println(" Chip firmware ver: " + String(VERSION_APP));
    
    #if defined(ESP32)
    Serial.println(" ESP32 WebPages version: "+ String(VERSION_WEB));
	#endif
    #if defined(ESP8266)
    Serial.println(" ESP8266 WebPages version: "+ String(VERSION_WEB));
	#endif
       
    Serial.print(" Build Date and time: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.println(__TIME__);
    
    
}
// // Эти макросы будут определены из Python-скрипта
// #ifndef GIT_BRANCH
// #define GIT_BRANCH "unknown"
// #endif

// #ifndef GIT_HASH
// #define GIT_HASH "unknown"
// #endif