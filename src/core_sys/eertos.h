#ifndef EERTOS_H
#define EERTOS_H

#include <stdint.h>



//RTOS Config
#define TaskQueueSize      	30
#define MainTimerQueueSize  30

typedef void (*TPTR)(void);
extern void Idle_task(void);
extern void InitRTOS(void);
extern void SetTask(TPTR TS);
extern void SetTaskFromISR(TPTR TS);
extern void SetTimerTask(TPTR TS, uint32_t NewTime);
extern void DelTimerTask(TPTR TS);
extern void TaskManager(void);

// Диагностические варианты: возвращают false при переполнении очереди.
// Старые void-функции реализованы поверх них.
extern bool SetTaskEx(TPTR TS);
extern bool SetTimerTaskEx(TPTR TS, uint32_t NewTime);
extern uint32_t EertosDroppedCount(void);

extern void TimerService(void);


#endif
