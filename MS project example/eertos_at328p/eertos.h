#ifndef EERTOS_H
#define EERTOS_H




//System Timer Config
#define Prescaler	  			64
#define	TimerDivider  		(F_CPU/Prescaler/1000)		// 1 mS



//RTOS Config
#define	TaskQueueSize		20
#define MainTimerQueueSize	20

typedef void (*TPTR)(void);
extern void InitRTOS(void);
extern void Idle(void);
extern void SetTask(TPTR TS);
extern void SetTimerTask(TPTR TS, uint16_t NewTime);
extern void DelTimerTask(TPTR TS);
extern void TaskManager(void);

extern void TimerService(void);


#endif
