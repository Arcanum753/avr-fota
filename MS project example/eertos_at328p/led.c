/*
 * led.c
 *
 * Created: 02.06.2021 14:07:47
 *  Author: sam
 */ 
#include <avr/io.h>
#include "led.h"
#include "common.h"
void LedInit (void){
	SetBit   (LED_DDR, LED_1);
	ClearBit(LED_PORT, LED_1);
	//
	SetBit   (LED_DDR2, LED_2);
	ClearBit(LED_PORT2, LED_2);
}