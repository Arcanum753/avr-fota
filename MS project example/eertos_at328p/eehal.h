/*
 * eehal.h
 *
 * Created: 13.06.2024 10:32:50
 *  Author: sam
 */ 


#ifndef EEHAL_H_
#define EEHAL_H_

//Clock Config
#define F_CPU 16000000L
#define HI(x) ((x)>>8)
#define LO(x) ((x)& 0xFF)


#define STATUS_REG 			SREG
#define Interrupt_Flag		SREG_I
#define Disable_Interrupt	cli
#define Enable_Interrupt	sei


#if (defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__)	)
#define RTOS_ISR	TIMER2_COMP_vect
#endif

#if ( defined(__AVR_ATmega32u4__)	)
#define RTOS_ISR	TIMER3_COMPA_vect
#endif

#if ( defined(__AVR_ATmega328P__)	)
#define RTOS_ISR	TIMER2_COMPA_vect
#endif


extern void RunRTOS (void);



#endif /* EEHAL_H_ */