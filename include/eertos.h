#ifndef EERTOS_H
#define EERTOS_H

#include <stdint.h>



//RTOS Config
#define TaskQueueSize      	30
#define MainTimerQueueSize  30

typedef void (*TPTR)(void);
extern void InitRTOS(void);
extern void Idle(void);
extern void SetTask(TPTR TS);
extern void SetTimerTask(TPTR TS, uint32_t NewTime);
extern void DelTimerTask(TPTR TS);
extern void TaskManager(void);

extern void TimerService(void);


#endif
 