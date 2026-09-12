#include <Arduino.h>

#include "main.h"

#include <ESPAsyncWebServer.h>
#include "core_web/FSWebServerLib.h"
#include "ErriezSerialTerminal.h"

#include "core_terminal/core_terminal.h"
#include "core_ntp/core_ntp.h"
#include "core_led/core_led.h"
#include "core_sys/core_sys.h"
#include "core_sys/ident_store.h"

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#if defined(PROGTYPE_SWD)
#include "module_program/submodule_swd/swd.h"
#endif

// ============================================================
// Конкретная логика модуля (реализации команд)
// ============================================================

// function for Terminal to show available commands
void TerminalHelp (void)    {   term.helpShow();    }
void TerminalEcho (void)    {   term.EchoOnOff();  }
// Terminals shows actual net info
void InfoShow() {    
    printGitInfo();
    core_sys.serialShowAbout();  
}
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
    #endif
    #if defined(ESP8266)
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
#if defined(MODULE_UDP)
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
    String str = module_udp.jsonGet();
    module_udp.broadcastSend(port, str);
#endif
}

void udpc ()    {
#if defined(MODULE_UDP)
  broadcastSimple();
#endif
}
void udps ()    {
#if defined(MODULE_UDP)
    module_udp.broadcastSend(module_udp.getPortTx(), "test");
#endif
}

void BlinkCmd(){

    String arg1;
    arg1 = term.getNext();

    if (arg1 == NULL) {
        Serial.println("Please set times to blink. ");
        return;
    }
    int16_t times = arg1.toInt();

    String arg2;
    arg2 = term.getNext();
    if (arg2 == NULL) {
        Serial.println("Please set blink mask. ");
        return;
    }
    if (arg2.length() > LEDSTRINGLIMIT){
        Serial.println("Blink mask is more than 200 slots. ");
        return;
    }
    LedMacroSet(  arg2.c_str(), times);
}

// Команда id: показать/установить имя и серийник устройства.
// Идентичность хранится в энергонезависимом хранилище (не стирается при обновлении FS).
void TermIdent() {
    char *arg1 = term.getNext();

    if (arg1 == NULL) {
        Serial.printf("Device name:   %s\r\n", core_sys.getDeviceName().c_str());
        Serial.printf("Device serial: %s\r\n", core_sys.getDeviceSerial().c_str());
        Serial.println("Usage: id <serial> | id name <name> | id reset");
        return;
    }

    String cmd(arg1);
    if (cmd == "reset") {
        // Восстановить дефолт: имя платформы + уникальный ID чипа
        core_sys.defaultConfigSys();
    }
    else if (cmd == "name") {
        char *arg2 = term.getNext();
        if (arg2 == NULL) {
            Serial.println("Usage: id name <name>");
            return;
        }
        String name(arg2);
        if (name.length() == 0 || name.length() > IDENT_MAX_NAME) {
            Serial.printf("Error: name length must be 1..%d\r\n", IDENT_MAX_NAME);
            return;
        }
        core_sys.setDeviceName(name);
    }
    else {
        String serial(arg1);
        if (serial.length() == 0 || serial.length() > IDENT_MAX_SERIAL) {
            Serial.printf("Error: serial length must be 1..%d\r\n", IDENT_MAX_SERIAL);
            return;
        }
        core_sys.setDeviceSerial(serial);
    }

    core_sys.saveSysIdentStore();
    core_sys.save_configSys();
    Serial.println("Identity saved. Reboot needed to apply hostname/mDNS.");
    Serial.printf("Device name:   %s\r\n", core_sys.getDeviceName().c_str());
    Serial.printf("Device serial: %s\r\n", core_sys.getDeviceSerial().c_str());
}
