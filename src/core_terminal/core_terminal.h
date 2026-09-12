#include <Arduino.h>
#include "ErriezSerialTerminal.h"
#include "core_terminal_types.h"

extern SerialTerminal term;

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

void TermIdent();

// String  fsDirListGet(); //DEBUG
