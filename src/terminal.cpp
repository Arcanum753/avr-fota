#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "FSWebServerLib.h"
#include "ErriezSerialTerminal.h"
#include "terminal.h"
#include "avrisp.h"

// Newline character '\r' or '\n'
char newlineChar = '\r';
// Separator character between commands and arguments
char delimiterChar = ' ';

SerialTerminal term(newlineChar, delimiterChar);

// ESP8266_AVRISP avrprog( PIN_RST);

void ledInit(){
    
    pinMode(PIN_MISO, OUTPUT);
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_SCK, OUTPUT);
    pinMode(PIN_RST, OUTPUT);

}

void terminalInit(){
    term.addCommand("?", showInfo );

    term.addCommand("1", test );
    term.addCommand("reset", resetEsp );

    
    term.addCommand("dir", dir);
    term.addCommand("check", check );
    term.addCommand("flash", flash1 );
    term.addCommand("flash2", flash2 );

    Serial.println("\n\r Serial terminal inited.");

}

void terminalLoop(){
    term.readSerial();
}
void test()     {  Serial.printf("\n\r test ok! \n\r");    }

void showInfo(){
    ESPHTTPServer.serialShowInfo();
}

void resetEsp(){
    Serial.printf("\n\r resetting! \n\r");
    delay (1000);
    ESP.reset();
}

void dir()      {  Serial.printf("list of files: \n\r %s \n\r", avrprog.fsDirListGet().c_str()); }
void check()    {  /*avrprog.avrCheckHex(0); */  }

void flash1   (){ 
    String arg1;
    arg1 = term.getNext();
    if (arg1 == NULL) return;
     Serial.printf("%d \n\r", avrprog.avrChipProgrammMain(arg1, " "));   
}
void flash2   (){ 
    String arg1;
    arg1 = term.getNext();
    if (arg1 == NULL) return;
     Serial.printf("%d \n\r", avrprog.avrChipProgrammDBG(arg1));   
     
}


