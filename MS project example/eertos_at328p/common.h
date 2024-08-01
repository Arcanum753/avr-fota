/*
 * common.h
 *
 * Created: 05.11.2020 14:59:24
 *  Author: sam
 */ 


#ifndef COMMON_H_
#define COMMON_H_


//макросы для работы с битами
#define InvBit(reg, bit)		 reg ^= (1<<bit)
#define ClearBit(reg, bit)		 reg &= ~(1<<bit)
#define SetBit(reg, bit)		 reg |= (1<<bit)
#define BitIsSet(reg, bit)		 ((reg & (1<<bit)) != 0)
#define BitIsClear(reg, bit)	 ((reg & (1<<bit)) == 0)


#endif /* COMMON_H_ */