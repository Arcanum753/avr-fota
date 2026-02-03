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
#include "module_udp.h"
#include "swd.h"






// Newline character '\r' or '\n'
char newlineChar = '\r';
// Separator character between commands and arguments
char delimiterChar = ' ';

SerialTerminal term(newlineChar, delimiterChar);

void TerminalInit(){
    term.addCommand("help",  TerminalHelp );    // показать все команды
    term.addCommand("reset", EspReset );        // ресет мк
    term.addCommand("echo",  TerminalEcho );    // выкл/вкл "эха" терминала.
    term.addCommand("?",     InfoShow );        // общая информация и текущее состояние подключения вай-фай

    term.addCommand("1",    test ); // тест терминала

    term.addCommand("udpp",    udpp ); 
    term.addCommand("udpc",    udpc ); 
    term.addCommand("udps",    udps ); 

// dead monks
// all about dbg of AVR
    // term.addCommand("flash", flash1 );
    // term.addCommand("flash2", flash2 );

    // term.addCommand("stm32",    termStm32 );
    // term.addCommand("swdf", termSwdFlash1 );
    // term.addCommand("avr",  avr );

    Serial.println("\n\r Serial terminal inited.");
    term.setSerialEcho(true);
    term.helpShow();
}

// main Loop func for Terminal
void TerminalLoop() {    term.readSerial(); }
// function for Terminal to show available commands
void TerminalHelp (void)    {   term.helpShow();    }
void TerminalEcho (void)    {   term.EchoOnOff();  }
// Terminals shows actual net info
void InfoShow() {    ESPHTTPServer.serialShowInfo();  }
// show list of files at SPIFS
void DirsShow() {
    // Serial.printf("list of files: \n\r %s \n\r", avrprog.fsDirListGet().c_str());
    // avrprog.filesClean();
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

void test()     {  Serial.printf("\n\r\t test ok! \n\r");    }

void avr() {
    //avrprog.begin();
    //avrprog.chipSignRead();
}



// void termStm32() {
//     String arg1 = " ";
//     swd_gpio_init();    //gpio initing
//     swdprog.stm32Fx_begin();      // stm32 chip init to work with swd and read Chip ID
//     while (arg1 != NULL) {
//         arg1 = term.getNext();
//         if (arg1 == "1") {  swdprog.stm32Fx_abort_all();                }
//         if (arg1 == "h") {  swdprog.stm32Fx_halt();                     }     // halt stm32 chip
//         if (arg1 == "e") {  swdprog.stm32f1_unlock_erase_flash();       }  // unlock flash mem + flash mem full erase
//         if (arg1 == "p") {  swdprog.stm32f1_progEn();                   }           // enable PG
//         if (arg1 == "f") {  termSwdFlash();                             }       // flashing
//         if (arg1 == "u") {  swdprog.stm32Fx_unhalt();                   }   // unhalt stm32 chip
//         if (arg1 == "r") {  swdprog.stm32Fx_rst();                      } // soft reset stm32 chip
//     }
// }
//  stm32 1 h e p f u r
// stm32 1 h u r
// void termSwdFlash1() {
//     String filename = "/stm32f103_dirtos100.binary";

//     String arg1;
//     arg1 = term.getNext();
//     if (arg1 == NULL) {  arg1 = "1";  }

//     if (arg1 == "1") { filename = "/stm32f103_dirtos100.binary";  }
//     if (arg1 == "2") { filename = "/stm32f103_dirtos500.binary";  }
//     if (arg1 == "3") { filename = "/stm32f103_dirtos1000.binary";  }

//     Serial.printf(" file: %s \n\r", filename.c_str());

//                     //gpio
//     swdprog.stm32Fx_begin();                  // stm32 chip init to work with swd and read Chip ID
//     swdprog.stm32Fx_abort_all(); // TODO
//     swdprog.stm32Fx_halt();                   // halt
//     swdprog.stm32f1_unlock_erase_flash();   // unlock mem + erase mem
//     swdprog.stm32f1_progEn();

//     swdprog.stm32_flash_file(FLASH_START_ADDR, filename) ;

//     //swdprog.stm32f1_progOff();
//     swdprog.stm32Fx_unhalt();
//     swdprog.stm32Fx_rst();
//     Serial.printf("\n\r fin! \n\r");
// }


// void termSwdFlash() {
//     String filename = "/stm32f103_dirtos100.binary";
//     Serial.printf(" file: %s \n\r", filename.c_str());
//     swdprog.stm32_flash_file(FLASH_START_ADDR, filename) ;
//     Serial.printf("\n\r fin! \n\r");
// }

// void flash1   ()    {
//     String arg1;
//     arg1 = term.getNext();
//     if (arg1 == NULL) return;
//      Serial.printf("%d \n\r", avrprog.avr_ChipProgrammMain(arg1, " "));
// }
// void flash2   ()    {
//     String arg1;
//     arg1 = term.getNext();
//     if (arg1 == NULL) return;
//      Serial.printf("%d \n\r", avrprog.avrChipProgrammDBG(arg1));
// }

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
    String str = udpBroadcast.udpJsonGet();
    udpBroadcast.udpBroadcastSend(port, str);
}

void udpc ()    {
     udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), udpBroadcast.udpJsonGet());
}


void udps ()    {
     udpBroadcast.udpBroadcastSend(udpBroadcast.getUpdPortTx(), "test");
    //   udp_Dbg.broadcastTo("test", 40000);
}

