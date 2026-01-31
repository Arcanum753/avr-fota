
#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif
#include <ESPAsyncWebServer.h>
#include <Ticker.h>
#include <ESPAsyncWebServer.h>

#include "main.h"
#include "FSWebServerLib.h"
#include "eertos.h"
#include "ErriezSerialTerminal.h"
#include "terminal.h"

#include "udphelper.h"

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

	// WiFi is started inside library
    ESPHTTPServer.begin(&SPIFFS);
    #if defined(ESP32)
	
	#endif
    Serial.print("*** Ep8266 service chip firmware ver: ");
    Serial.print(VERSION_APP);
    Serial.println(" ***");

    Serial.print("*** Ep8266 web pages ver: ");
    Serial.print(VERSION_WEB);
    Serial.println(" ***");

    Serial.print("*** build DateTime: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.print(__TIME__);
    Serial.println(" ***");
   
    TerminalInit();
    
    
    
}

void loop() {
    TaskManager();
    loop_user();
    TerminalLoop();
//    ESPHTTPServer.handle(); FIXME ??
    ArduinoOTA.handle();
    
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
