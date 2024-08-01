/*
 * eehal.c
 *
 * Created: 13.06.2024 10:33:11
 *  Author: sam
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "eehal.h"
#include "eertos.h"

// код зависимый от чипа
//RTOS Запуск системного таймера
inline void RunRTOS (void)	{
#if (defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__) 	)
	// инициализация таймера 2
	TCCR2 = 1<<WGM21|4<<CS20; 				// Freq = CK/64 - Установить режим и предделитель
	// Автосброс после достижения регистра сравнения
	TCNT2 = 0;								// Установить начальное значение счётчиков
	OCR2  = LO(TimerDivider); 				// Установить значение в регистр сравнения
	TIMSK = 0<<TOIE0|1<<OCF2|0<<TOIE0;
	// Разрешаем прерывание RTOS - запуск ОС
#endif

#if ( defined(__AVR_ATmega32u4__)	)
	TCNT3H = 0;
	TCNT3L = 0;
	// инициализация таймера 3
	TCCR3B = 0<<CS32 | 1<<CS31 | 1<<CS30 | 1<<WGM32; // Freq = CK/64 - Установить режим и предделитель
	// Автосброс после достижения регистра сравнения
	TCCR3A = 0<<WGM31 | 0<<WGM30;
	// Установить значение в регистр сравнения
	OCR3A  = LO(TimerDivider);
	TIMSK3 = 1<<OCIE3A;
#endif

#if ( defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168P__)	)
	// инициализация таймера 2
	TCCR2A = 1<<WGM21; 				// Freq = CK/64 - Установить режим и предделитель ;
	TCCR2B = 4<<CS20;
	// Автосброс после достижения регистра сравнения
	TCNT2 = 0;								// Установить начальное значение счётчиков
	OCR2A  = LO(TimerDivider); 				// Установить значение в регистр сравнения
	TIMSK2 = 1<<OCIE2A;
	// Разрешаем прерывание RTOS - запуск ОС
#endif

}