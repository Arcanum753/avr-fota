/*!
 * \file ErriezSerialTerminal.cpp
 * \brief Serial terminal library for Arduino.
 * \details
 *      Source:         https://github.com/Erriez/ErriezSerialTerminal
 *      Documentation:  https://erriez.github.io/ErriezSerialTerminal
 */
#include <Arduino.h>

#include "main.h"

#include <ESPAsyncWebServer.h>
#include "FSWebServerLib.h"
#include "ErriezSerialTerminal.h"
#include "terminal.h"
#include "avrisp.h"
#include "udphelper.h"

// Newline character '\r' or '\n'
char newlineChar = '\r';
// Separator character between commands and arguments
char delimiterChar = ' ';

SerialTerminal term(newlineChar, delimiterChar);

void TerminalInit(){
    term.addCommand("help", TerminalHelp );
    term.addCommand ("echo", TerminalEcho);
    term.addCommand("?", InfoShow );


    term.addCommand("1", test );
    term.addCommand("reset", EspReset );

    
    term.addCommand("dir", DirsShow);
    term.addCommand("check", check );
    term.addCommand("flash", flash1 );
    term.addCommand("flash2", flash2 );
    term.addCommand ("udpp", udpp);
    term.addCommand ("udpc", udpc);

    Serial.println("\n\r Serial terminal inited.");
    term.setSerialEcho(true);
    term.helpShow();
}

// main Loop func for Terminal
void TerminalLoop() {    term.readSerial(); }
// function for Terminal to show available commands
void TerminalHelp (void)    {   term.helpShow();}
void TerminalEcho (void)    {   term.EchoOnOff();  }
// Terminals shows actual net info
void InfoShow() {    ESPHTTPServer.serialShowInfo();  }
// show list of files at SPIFS
void DirsShow() {   
    Serial.printf("list of files: \n\r %s \n\r", avrprog.fsDirListGet().c_str()); 
    avrprog.filesClean(); 
}

// Reset Esp
void EspReset(){
    #ifdef ESP32
    Serial.printf("\n\rESP32 reset now! \n\r");
    delay (1000);
    ESP.restart();
    #elif defined(ESP8266)
    Serial.printf("\n\rESP8266 reset now! \n\r");
    delay (1000);
    ESP.reset();
#endif
  
}

void test()     {  Serial.printf("\n\r test ok! \n\r");    }



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

void udpp (){
    String arg1;
    arg1 = term.getNext();
    if (arg1 == NULL) {
        Serial.println("Please set port. ");
        return; 
    }
    uint16_t port = arg1.toInt();
    if (port < 10000 || port >= 65536) {
        Serial.println("Please set port 10000 < port <= 65536");
        return; 
    }
    String str = ESPHTTPServer.udpJsonBroadcast();
    udpBroadcast.udpBroadcastSend(port, str);

}

void udpc (){
     udpBroadcast.udpBroadcastSend(ESPHTTPServer.getUpdPortTx(), ESPHTTPServer.udpJsonBroadcast());
}


