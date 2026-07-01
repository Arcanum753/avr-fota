#include <Arduino.h>
#include "ErriezSerialTerminal.h"

extern SerialTerminal term;

typedef void (*TerminalModuleInit)(void);
#define TERMINAL_MODULE_SLOTS 8
void TerminalRegisterModule(TerminalModuleInit initFn);

void TerminalInit();
void TerminalLoop();
void TerminalHelp (void);
void TerminalEcho (void) ;


void test  ();
void InfoShow ();
void EspReset();

void DirsShow  ();
void check  ();

void flash1  ();
void flash2  ();

void termStm32() ;

void termSwdFlash1() ;
void termSwdFlash();

void udpp ();
void udpc ();
void udps ();
void BlinkCmd();

void avr() ;

// String  fsDirListGet(); //DEBUG
