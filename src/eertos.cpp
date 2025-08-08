/*
 * eertos.cpp
 *
 *  Created on: 13.06.2024
 *      Author: Sam
 */
#include <stdint.h>
#include "eertos.h"

static void Idle_task(void);

/* Tasks queue with pointers to the runnable functions */
static TPTR TaskQueue[TaskQueueSize];

/* Timers queue. On zero value TimerService() will
 * put GoToTask to the TaskQueue[]
 */
static struct
{
	TPTR	 GoToTask;
	uint32_t Time;
} MainTimer[MainTimerQueueSize];

/**
 * @brief Dummy procedure. Just do nothing
 *
 */
void
Idle_task(void)
{}

/**
 * @brief Initialise Tasks and Timer Queue
 * All tasks in the queue are Idle_task
 * All timers zeroed
 */
void
InitRTOS(void)
{
	for (uint32_t index = 0; index < TaskQueueSize; index++)
	{
		TaskQueue[index] = Idle_task;
	}

	for (uint32_t index = 0; index < MainTimerQueueSize; index++)
	{
		MainTimer[index].GoToTask = Idle_task;
		MainTimer[index].Time	  = 0;
	}
}

/**
 * @brief Put the task to the queue. Quit silently in case of no free slot
 *
 * @param TS Pointer to the task function
 */
void
SetTask(TPTR TS)
{
	for (uint32_t index = 0; index < TaskQueueSize; index++)
	{
		if (TaskQueue[index] == Idle_task)
		{
			TaskQueue[index] = TS;
			return;
		}
	}
	/*
	 * Here you can put any error procedure code in the future
	 * if didn't find any free slot
	 */
}

/**
 * @brief Set the Timer Task object
 * Checks Timers queue to find desired task function pointer
 * Updates timer if pointer exists
 * Add new pointer if doesn't
 * @param TS Pointer to the task function
 * @param NewTime New timer value
 */
void
SetTimerTask(TPTR TS, uint32_t NewTime)
{
	for (uint32_t index = 0; index < MainTimerQueueSize; index++)
	{
		if (MainTimer[index].GoToTask == TS)
		{
			MainTimer[index].Time = NewTime;
			return;
		}
	}

	for (uint32_t index = 0; index < MainTimerQueueSize; index++)
	{
		if (MainTimer[index].GoToTask == Idle_task)
		{
			MainTimer[index].GoToTask = TS;
			MainTimer[index].Time	  = NewTime;
			return;
		}
	}
}

/**
 * @brief Remove timer from queue
 * Removes timer for desired task, so it will not be run
 * @param TS Pointer to the task function
 */
void
DelTimerTask(TPTR TS)
{
	for (uint32_t index = 0; index < MainTimerQueueSize; index++)
	{
		if (MainTimer[index].GoToTask == TS)
		{
			MainTimer[index].GoToTask = Idle_task;
			MainTimer[index].Time	  = 0;
			return;
		}
	}
}

/**
 * @brief EERTOS Task manager function
 * Shift queue and run function that was first before shift
 */
void
TaskManager(void)
{
	TPTR	 GoToTask = TaskQueue[0];
	if (GoToTask == Idle_task)
	{
		(Idle_task)();
	}
	else
	{
		/* Shift tasks to the start of queue */
		for (uint32_t index = 0; index < (TaskQueueSize - 1); index++)
		{
			TaskQueue[index] = TaskQueue[index + 1];
		}
		/* Push Idle_task to the last slot */
		TaskQueue[TaskQueueSize] = Idle_task;
		(GoToTask)();
	}
}

/**
 * @brief Core Timer service. Usually should be called on 1ms tick
 * TODO: Think about dynamic timers queue, than can be huge. We will call it
 * "Heap killer"
 */
void
TimerService(void)
{
	for (uint32_t index = 0; index < MainTimerQueueSize; index++)
	{
		/* On Idle_task go to the next slot */
		if (MainTimer[index].GoToTask == Idle_task)
			continue;

		/* Decrease timer and push task to the queue on zero */
		if (--MainTimer[index].Time == 0)
		{
			SetTask(MainTimer[index].GoToTask);
			MainTimer[index].GoToTask = Idle_task;
		}
	}
}
