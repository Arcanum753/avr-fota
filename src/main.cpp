#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Ticker.h>

#include "FSWebServerLib.h"
#include "main.h"
#include"eertos.h"
#include "ErriezSerialTerminal.h"
#include "terminal.h"
#include "avrisp.h"

// pin used for entering setup mode

// Ticker _secondEERtos;
void usecondTick();
void TaskBlink1();
void TaskBlink2();
// void loop_user();

// unsigned long previousMillis = 0;
// unsigned long interval = 10000;

void setup() {
  
    Serial.begin(115200);
    InitRTOS(); // init eertos
    // _secondEERtos.attach_ms(1, &usecondTick); // init eertos time manager   
    // ledInit();
    // SetTask(TaskBlink1); // do blink
    SPIFFS.begin(); // Not really needed, checked inside library and started if
                    // needed
                    
    // WiFi is started inside library
    ESPHTTPServer.begin(&SPIFFS);
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
    terminalInit();
    
    
}

void loop() {
    // loop_user();
    TaskManager();
    terminalLoop();
    ESPHTTPServer.handle();
}

void loop_user(){
    // DO NOT REMOVE. Attend OTA update from Arduino IDE
    // unsigned long currentMillis = millis();

    // if (currentMillis - previousMillis > interval) {
    //     previousMillis = currentMillis;

    //     String T = "";
    //     int I = 0;
    //     float F = 0.0;

    //     ESPHTTPServer.load_user_config("value1", T);
    //     Serial.print("value1: ");
    //     Serial.println(T);
    //     Serial.println("");

        
    // }
}



void TaskBlink1(){
    
    digitalWrite(PIN_MISO, HIGH);
    digitalWrite(PIN_MOSI, HIGH);
    digitalWrite(PIN_SCK, HIGH);
    digitalWrite(PIN_RST, HIGH);
    
    SetTimerTask(TaskBlink2, 3000);
}

void TaskBlink2(){
    digitalWrite(PIN_MISO, LOW); 
    digitalWrite(PIN_MOSI, LOW);
    digitalWrite(PIN_SCK, LOW);
    digitalWrite(PIN_RST, LOW);

    
    //  SetTimerTask(TaskBlink1, 1000);
}

void usecondTick()  {
    TimerService();
}