/*
 * led.h
 *
 * Created: 02.06.2021 14:06:14
 *  Author: sam
 */ 


#ifndef LED_H_
#define LED_H_

#define		LED_PORT 		PORTB
#define		LED_DDR 		DDRB
#define		LED_1 			5


#define		LED_PORT2 		PORTD
#define		LED_DDR2 		DDRD
#define		LED_2 			0


#define  LED_1_ON			SetBit  (LED_PORT, LED_1)
#define  LED_1_OFF			ClearBit(LED_PORT, LED_1)

#define  LED_2_ON			ClearBit(LED_PORT2, LED_2)
#define  LED_2_OFF			SetBit	(LED_PORT2, LED_2)
void LedInit (void);


#endif /* LED_H_ */