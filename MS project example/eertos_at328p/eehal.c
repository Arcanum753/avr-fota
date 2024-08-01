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

// ��� ��������� �� ����
//RTOS ������ ���������� �������
inline void RunRTOS (void)	{
#if (defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__) 	)
	// ������������� ������� 2
	TCCR2 = 1<<WGM21|4<<CS20; 				// Freq = CK/64 - ���������� ����� � ������������
	// ��������� ����� ���������� �������� ���������
	TCNT2 = 0;								// ���������� ��������� �������� ���������
	OCR2  = LO(TimerDivider); 				// ���������� �������� � ������� ���������
	TIMSK = 0<<TOIE0|1<<OCF2|0<<TOIE0;
	// ��������� ���������� RTOS - ������ ��
#endif

#if ( defined(__AVR_ATmega32u4__)	)
	TCNT3H = 0;
	TCNT3L = 0;
	// ������������� ������� 3
	TCCR3B = 0<<CS32 | 1<<CS31 | 1<<CS30 | 1<<WGM32; // Freq = CK/64 - ���������� ����� � ������������
	// ��������� ����� ���������� �������� ���������
	TCCR3A = 0<<WGM31 | 0<<WGM30;
	// ���������� �������� � ������� ���������
	OCR3A  = LO(TimerDivider);
	TIMSK3 = 1<<OCIE3A;
#endif

#if ( defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168P__)	)
	// ������������� ������� 2
	TCCR2A = 1<<WGM21; 				// Freq = CK/64 - ���������� ����� � ������������ ;
	TCCR2B = 4<<CS20;
	// ��������� ����� ���������� �������� ���������
	TCNT2 = 0;								// ���������� ��������� �������� ���������
	OCR2A  = LO(TimerDivider); 				// ���������� �������� � ������� ���������
	TIMSK2 = 1<<OCIE2A;
	// ��������� ���������� RTOS - ������ ��
#endif

}