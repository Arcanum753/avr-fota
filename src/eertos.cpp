/*
 * eertos.cpp
 *
 *  Created on: 13.06.2024
 *      Author: Sam
 */
#include <stdint.h>
#include "EERTOS.h"

//Пустая процедура - простой ядра.
void  Idle_task(void) { }


// Очереди задач, таймеров.
// Тип данных - указатель на функцию
static TPTR TaskQueue[TaskQueueSize+1];         	// очередь указателей

static struct
{
  TPTR GoToTask;                   	// Указатель перехода
  uint32_t Time;                    // Выдержка в мс
}

MainTimer[MainTimerQueueSize+1];  // Очередь таймеров


// RTOS Подготовка. Очистка очередей
void InitRTOS(void)	{
	uint32_t   index;
	for(index=0;index!=TaskQueueSize+1;index++)   // Во все позиции записываем Idle
	{
		TaskQueue[index] = Idle_task;
	}
	for(index=0;index!=MainTimerQueueSize+1;index++) // Обнуляем все таймеры.
	{
		MainTimer[index].GoToTask = Idle_task;
		MainTimer[index].Time = 0;
	}
}

// Функция установки задачи в очередь. Передаваемый параметр - указатель на функцию
// Отдаваемое значение - код ошибки.
void SetTask(TPTR TS)	{
	uint32_t      index = 0;
	while(TaskQueue[index]!=Idle_task)          // Прочесываем очередь задач на предмет свободной ячейки
	{                           				// с значением Idle - конец очереди.
		index++;
		if (index==TaskQueueSize+1)       		// Если очередь переполнена то выходим не солоно хлебавши
		{
			return;                           	// Раньше функция возвращала код ошибки - очередь переполнена. Пока убрал.
		}
	}
											// Если нашли свободное место, то
	TaskQueue[index] = TS;                     	// Записываем в очередь задачу	__enable_fault_irq();
}


//Функция установки задачи по таймеру. Передаваемые параметры - указатель на функцию,
// Время выдержки в тиках системного таймера. Возвращет код ошибки.
void SetTimerTask(TPTR TS, uint32_t NewTime)	{
	uint32_t      index = 0;
	uint32_t      Idle_i = 0;
	for(index=0;index!=MainTimerQueueSize+1;++index)   				//Прочесываем очередь таймеров
	   {
	   if(MainTimer[index].GoToTask == TS)            				// Если уже есть запись с таким адресом
		  {
		  MainTimer[index].Time = NewTime;         					// Перезаписываем ей выдержку
		   return;                              					// Выходим. Раньше был код успешной операции. Пока убрал
		  }
	   else
		  {
		  if ((MainTimer[index].GoToTask == Idle_task) && (Idle_i == 0))
			 {
			 Idle_i = index;
			 }
		  }
	   }

	for(index=0;index!=MainTimerQueueSize+1;++index)   				// Если не находим похожий таймер, то ищем любой пустой
	   {
	   if (MainTimer[index].GoToTask == Idle_task)
		  {
            MainTimer[index].GoToTask = TS;         					// Заполняем поле перехода задачи
            MainTimer[index].Time = NewTime;      					// И поле выдержки времени
            return;                           						// Выход.
		  }

	   }                                    						// тут можно сделать return c кодом ошибки - нет свободных таймеров
}

/*
// функция удаления задачи из очереди  таймеров. // Sam_Arcanum 
*/
void DelTimerTask(TPTR TS)	{
	uint8_t		index=0;
	// uint8_t		nointerrupted = 0;
	for(index=0; index!=MainTimerQueueSize+1; ++index)	//Прочесываем очередь таймеров
	{
		if(MainTimer[index].GoToTask == TS)				// Если уже есть запись с таким адресом
		{
			MainTimer[index].GoToTask = Idle;			// Заполняем поле перехода задачи пустышкой.
			MainTimer[index].Time = 0;
			return;										// Выходим. Раньше был код успешной операции. Пока убрал
		}
	}
}

/*=================================================================================
Диспетчер задач ОС. Выбирает из очереди задачи и отправляет на выполнение.
*/

void TaskManager(void)	{
	uint32_t      index=0;
	TPTR   GoToTask = Idle_task;      	// Инициализируем переменные
	GoToTask = TaskQueue[0];      		// Хватаем первое значение из очереди
	if (GoToTask==Idle_task)          	// Если там пусто
	   {
    	   (Idle_task)();                	// Переходим на обработку пустого цикла
	   }
	else    {
	   for(index=0;index!=TaskQueueSize;index++)   		// В противном случае сдвигаем всю очередь
		  {
		      TaskQueue[index]=TaskQueue[index+1];
		  }

    	   TaskQueue[TaskQueueSize]= Idle_task;            	// В последнюю запись пихаем затычку
	       (GoToTask)();                        			// Переходим к задаче
	   }
}


/*
Служба таймеров ядра. Должна вызываться из прерывания раз в 1мс. Хотя время можно варьировать в зависимости от задачи

TODO: Привести к возможности загружать произвольную очередь таймеров. Тогда можно будет создавать их целую прорву.
А также использовать эту функцию произвольным образом.
В этом случае не забыть добавить проверку прерывания.
*/
void TimerService(void)	{
	uint32_t index;
	for(index=0;index!=MainTimerQueueSize+1;index++)      			// Прочесываем очередь таймеров
	   {
	   if(MainTimer[index].GoToTask == Idle_task) continue;      	// Если нашли пустышку - щелкаем следующую итерацию
	   if(MainTimer[index].Time !=1)                  				// Если таймер не выщелкал, то щелкаем еще раз.
		  {                                    						// To Do: Вычислить по тактам, что лучше !=1 или !=0.
		    MainTimer[index].Time --;                  				// Уменьшаем число в ячейке если не конец.
		  }
	   else {
		    SetTask(MainTimer[index].GoToTask);            			// Дощелкали до нуля? Пихаем в очередь задачу
		    MainTimer[index].GoToTask = Idle_task;            		// А в ячейку пишем затычку
		  }
	   }
}

