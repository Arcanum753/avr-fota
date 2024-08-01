/*
 * pb2avr16.c
 *
 * Created: 02.06.2021 12:21:01
 * Author : sam
 */ 
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "eertos.h"
#include "eehal.h"
#include "avrlibtypes.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>




#include "led.h"
#include "main.h"
#include "common.h"

ISR(RTOS_ISR)	{
	TimerService();
}

void LedInitBlink(void);

int main(void){

	LedInit();
	LedInitBlink();
	InitRTOS();
	RunRTOS();
	SetTimerTask(Led1Blinking1, 1);
    /* Replace with your application code */
	while(1) {							// Главный цикл диспетчера
		wdt_reset();								// Сброс собачьего таймера
		TaskManager();								// Вызов диспетчера
	}
}

void Led1Blinking1 (void){
	LED_1_OFF;
	LED_2_OFF;
	SetTimerTask(Led2Blinking2, 100);
}
void Led2Blinking2 (void){
	LED_1_ON;
	LED_2_ON;
	SetTimerTask(Led1Blinking1, 500);
}


void LedInitBlink(void){
	LED_1_OFF;
	LED_2_ON;
	_delay_ms(550);
	LED_1_ON;
	LED_2_OFF;
	_delay_ms(300);
	LED_1_ON;
	LED_2_ON;
	_delay_ms(200);
	LED_1_OFF;
	LED_2_OFF;
	_delay_ms(300);
	LED_1_ON;
	LED_2_ON;
	_delay_ms(750);
	LED_1_OFF;
	LED_2_OFF;
	_delay_ms(1000);
	LED_1_ON;
	LED_2_ON;
	_delay_ms(100);
	LED_1_OFF;
	LED_2_OFF;
	_delay_ms(1000);
	LED_1_ON;
	LED_2_ON;
	_delay_ms(500);
	LED_1_OFF;
	LED_2_OFF;
	_delay_ms(1000);
	LED_1_ON;
	LED_2_ON;
	_delay_ms(700);
	LED_1_OFF;
	LED_2_ON;
	_delay_ms(100);
	LED_1_ON;
	LED_2_OFF;
	_delay_ms(100);
	LED_1_OFF;
	LED_2_ON;
	_delay_ms(500);
	LED_1_ON;
	LED_2_OFF;
	_delay_ms(150);
}