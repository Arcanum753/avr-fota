/*
 * eertos.cpp
 *
 *  Created on: 13.06.2024
 *      Author: Sam
 */

#include <stdint.h>
#include "eertos.h"
#include <Arduino.h>

#if defined(ESP32)
    #include <esp_attr.h>      // Для IRAM_ATTR
    #include <esp_intr_alloc.h> // Для управления прерываниями
    
    // Для ESP32 используем spinlock через порты
    static portMUX_TYPE eertosMux = portMUX_INITIALIZER_UNLOCKED;
    
    // В основном контексте
    #define EERTOS_ENTER_CRITICAL() portENTER_CRITICAL(&eertosMux)
    #define EERTOS_EXIT_CRITICAL()  portEXIT_CRITICAL(&eertosMux)
    
    // В прерываниях - специальная версия
    #define EERTOS_ENTER_CRITICAL_ISR() portENTER_CRITICAL_ISR(&eertosMux)
    #define EERTOS_EXIT_CRITICAL_ISR()  portEXIT_CRITICAL_ISR(&eertosMux)

#elif defined(ESP8266)
    // Для ESP8266 простые noInterrupts/interrupts
    #define EERTOS_ENTER_CRITICAL() noInterrupts()
    #define EERTOS_EXIT_CRITICAL()  interrupts()
    
    // На ESP8266 в прерываниях используем те же функции
    #define EERTOS_ENTER_CRITICAL_ISR() noInterrupts()
    #define EERTOS_EXIT_CRITICAL_ISR()  interrupts()

#else
    #error "Unsupported platform"
#endif

static void Idle_task(void);

/* Tasks queue with pointers to the runnable functions */
static TPTR TaskQueue[TaskQueueSize];

/* Timers queue. On zero value TimerService() will
 * put GoToTask to the TaskQueue[]
 */
static struct
{
    TPTR     GoToTask;
    uint32_t Time;
} MainTimer[MainTimerQueueSize];

/**
 * @brief Dummy procedure. Just do nothing
 *
 */
void Idle_task(void) {}

/**
 * @brief Initialise Tasks and Timer Queue
 * All tasks in the queue are Idle_task
 * All timers zeroed
 */
void InitRTOS(void) {
    for (uint32_t index = 0; index < TaskQueueSize; index++) { TaskQueue[index] = Idle_task; }
    for (uint32_t index = 0; index < MainTimerQueueSize; index++) {
        MainTimer[index].GoToTask = Idle_task;
        MainTimer[index].Time     = 0;
    }
}

/**
 * @brief Put the task to the queue. Quit silently in case of no free slot
 *
 * @param TS Pointer to the task function
 */
void SetTask(TPTR TS) {
    EERTOS_ENTER_CRITICAL();
    for (uint32_t index = 0; index < TaskQueueSize; index++) {
        if (TaskQueue[index] == Idle_task) {
            TaskQueue[index] = TS;
            EERTOS_EXIT_CRITICAL();
            return;
        }
    }
    EERTOS_EXIT_CRITICAL();
}

/**
 * @brief Put the task to the queue from ISR context.
 * Uses ISR-safe critical section on ESP32.
 * Quit silently in case of no free slot
 *
 * @param TS Pointer to the task function
 */
void SetTaskFromISR(TPTR TS) {
    EERTOS_ENTER_CRITICAL_ISR();
    for (uint32_t index = 0; index < TaskQueueSize; index++) {
        if (TaskQueue[index] == Idle_task) {
            TaskQueue[index] = TS;
            EERTOS_EXIT_CRITICAL_ISR();
            return;
        }
    }
    EERTOS_EXIT_CRITICAL_ISR();
}

/**
 * @brief Set the Timer Task object
 * Checks Timers queue to find desired task function pointer
 * Updates timer if pointer exists
 * Add new pointer if doesn't
 * @param TS Pointer to the task function
 * @param NewTime New timer value
 */
void SetTimerTask(TPTR TS, uint32_t NewTime) {
    EERTOS_ENTER_CRITICAL();
    
    uint32_t Idle_i = 0;
    // Сначала ищем существующий таймер для этой задачи
    for (uint32_t index = 0; index < MainTimerQueueSize; index++) {
        if (MainTimer[index].GoToTask == TS) {
            MainTimer[index].Time = NewTime;
            EERTOS_EXIT_CRITICAL();
            return;
        }
        // Запоминаем первый свободный слот
        if ((MainTimer[index].GoToTask == Idle_task) && (Idle_i == 0)) { Idle_i = index; }
    }

    // Если таймер не найден, используем первый свободный слот
    if (Idle_i < MainTimerQueueSize) {
        MainTimer[Idle_i].GoToTask = TS;
        MainTimer[Idle_i].Time = NewTime;
        EERTOS_EXIT_CRITICAL();
        return;
    }
    
    // Если свободных слотов нет - ищем любой свободный (на всякий случай)
    for (uint32_t index = 0; index < MainTimerQueueSize; index++) {
        if (MainTimer[index].GoToTask == Idle_task) {
            MainTimer[index].GoToTask = TS;
            MainTimer[index].Time = NewTime;
            EERTOS_EXIT_CRITICAL();
            return;
        }
    }
    
    EERTOS_EXIT_CRITICAL();
}

/**
 * @brief Remove timer from queue
 * Removes timer for desired task, so it will not be run
 * @param TS Pointer to the task function
 */
void DelTimerTask(TPTR TS) {
    EERTOS_ENTER_CRITICAL();
    for (uint32_t index = 0; index < MainTimerQueueSize; index++) {
        if (MainTimer[index].GoToTask == TS) {
            MainTimer[index].GoToTask = Idle_task;
            MainTimer[index].Time = 0;
            EERTOS_EXIT_CRITICAL();
            return;
        }
    }
    EERTOS_EXIT_CRITICAL();
}

/**
 * @brief EERTOS Task manager function
 * Shift queue and run function that was first before shift
 */
void TaskManager(void) {
    TPTR GoToTask;
    EERTOS_ENTER_CRITICAL();
    GoToTask = TaskQueue[0];
    if (GoToTask != Idle_task) {
        /* Shift tasks */
        for (uint32_t index = 0; index < (TaskQueueSize - 1); index++) { 
            TaskQueue[index] = TaskQueue[index + 1]; 
        }
        TaskQueue[TaskQueueSize - 1] = Idle_task;
    }
    EERTOS_EXIT_CRITICAL();
    
    if (GoToTask != Idle_task) { 
        GoToTask(); 
    } else { 
        Idle_task(); 
    }
}

/**
 * @brief Core Timer service. Usually should be called on 1ms tick
 */
void IRAM_ATTR TimerService(void) {
    for (uint32_t index = 0; index < MainTimerQueueSize; index++) {
        /* On Idle_task go to the next slot */
        if (MainTimer[index].GoToTask == Idle_task) { continue; }

        /* Decrease timer and push task to the queue on zero */
        if (--MainTimer[index].Time == 0) {
            // В прерывании используем ISR-версию критической секции
            EERTOS_ENTER_CRITICAL_ISR();
            
            TPTR taskToRun = MainTimer[index].GoToTask;
            MainTimer[index].GoToTask = Idle_task;
            
            // Выходим из ISR-критической секции
            EERTOS_EXIT_CRITICAL_ISR();
            
            // Используем ISR-безопасную версию SetTask
            // Это гарантирует корректную работу на ESP32 с spinlock
            SetTaskFromISR(taskToRun);
        }
    }
}
