#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <stdint.h>
#include <stdbool.h>
#include "eehal.h"
#include "eertos.h"


// ������� �����, ��������.
// ��� ������ - ��������� �� �������
volatile static TPTR	TaskQueue[TaskQueueSize+1];			// ������� ����������
volatile static struct 	
{									
	TPTR GoToTask; 						// ��������� ��������
	uint16_t Time;							// �������� � ��
} 

MainTimer[MainTimerQueueSize+1];	// ������� ��������


// RTOS ����������. ������� ��������
inline void InitRTOS(void)	{
	uint8_t	index;

	for(index=0; index!=TaskQueueSize+1; index++)	// �� ��� ������� ���������� Idle
		{
			TaskQueue[index] = Idle;
		}


	for(index=0;index!=MainTimerQueueSize+1;index++) // �������� ��� �������.
		{
			MainTimer[index].GoToTask = Idle;
			MainTimer[index].Time = 0;
		}
}



//������ ��������� - ������� ����. 
inline void  Idle(void)
{

}


// ������� ��������� ������ � �������. ������������ �������� - ��������� �� �������
// ���������� �������� - ��� ������.
void SetTask(TPTR TS)	{
	uint8_t		index = 0;
	uint8_t		nointerrupted = 0;

	if (STATUS_REG & (1<<Interrupt_Flag))  // ���� ���������� ���������, �� ��������� ��.
		{
			Disable_Interrupt();
			nointerrupted = 1;					// � ������ ����, ��� �� �� � ����������. 
		}

	while(TaskQueue[index]!=Idle) 			// ����������� ������� ����� �� ������� ��������� ������
		{									// � ��������� Idle - ����� �������.
			index++;
			if (index==TaskQueueSize+1) 		// ���� ������� ����������� �� ������� �� ������ ��������
				{
					if (nointerrupted)	Enable_Interrupt(); 	// ���� �� �� � ����������, �� ��������� ����������
					return;									// ������ ������� ���������� ��� ������ - ������� �����������. ���� �����.
				}
		}
													// ���� ����� ��������� �����, ��
	TaskQueue[index] = TS;							// ���������� � ������� ������
	if (nointerrupted) Enable_Interrupt();				// � �������� ���������� ���� �� � ����������� ����������.
}


//������� ��������� ������ �� �������. ������������ ��������� - ��������� �� �������, 
// ����� �������� � ����� ���������� �������. ��������� ��� ������.
void SetTimerTask(TPTR TS, uint16_t NewTime)	{
uint8_t		index=0;
uint8_t		nointerrupted = 0;

if (STATUS_REG & (1<<Interrupt_Flag)) 			// �������� ������� ����������, ���������� ������� ����
	{
		Disable_Interrupt();
		nointerrupted = 1;
	}


for(index=0; index!=MainTimerQueueSize+1; ++index)	//����������� ������� ��������
	{
	if(MainTimer[index].GoToTask == TS)				// ���� ��� ���� ������ � ����� �������
		{
			MainTimer[index].Time = NewTime;			// �������������� �� ��������
			if (nointerrupted) 	Enable_Interrupt();		// ��������� ���������� ���� �� ���� ���������.
			return;										// �������. ������ ��� ��� �������� ��������. ���� �����
		}
	}
	

for(index=0; index!=MainTimerQueueSize+1; ++index)	// ���� �� ������� ������� ������, �� ���� ����� ������	
	{
	if (MainTimer[index].GoToTask == Idle)		
		{
			MainTimer[index].GoToTask = TS;			// ��������� ���� �������� ������
			MainTimer[index].Time = NewTime;		// � ���� �������� �������
			if (nointerrupted) 	Enable_Interrupt();	// ��������� ����������
			return;									// �����. 
		}
		
	}												// ��� ����� ������� return c ����� ������ - ��� ��������� ��������
}


/*
// ������� �������� ������ �� �������  ��������. // Sam_Arcanum 
*/
void DelTimerTask(TPTR TS)	{
	uint8_t		index=0;
	uint8_t		nointerrupted = 0;

	if (STATUS_REG & (1<<Interrupt_Flag)) 			// �������� ������� ����������, ���������� ������� ����
	{
		Disable_Interrupt();
		nointerrupted = 1;
	}

	for(index=0; index!=MainTimerQueueSize+1; ++index)	//����������� ������� ��������
	{
		if(MainTimer[index].GoToTask == TS)				// ���� ��� ���� ������ � ����� �������
		{
			MainTimer[index].GoToTask = Idle;			// ��������� ���� �������� ������ ���������.
			MainTimer[index].Time = 0;
			if (nointerrupted) 	Enable_Interrupt();		// ��������� ���������� ���� �� ���� ���������.
			return;										// �������. ������ ��� ��� �������� ��������. ���� �����
		}
	}
	
	
}

/*=================================================================================
	��������� ����� ��. �������� �� ������� ������ � ���������� �� ����������.
*/

inline void TaskManager(void)
{
	uint8_t		index=0;
	TPTR	GoToTask = Idle;		// �������������� ����������

	Disable_Interrupt();				// ��������� ����������!!!
	GoToTask = TaskQueue[0];		// ������� ������ �������� �� �������

	if (GoToTask==Idle) 			// ���� ��� �����
		{
			Enable_Interrupt();			// ��������� ����������
			(Idle)(); 					// ��������� �� ��������� ������� �����
		}
	else
		{
			for(index=0;index!=TaskQueueSize;index++)	// � ��������� ������ �������� ��� �������
				{
					TaskQueue[index]=TaskQueue[index+1];
				}

			TaskQueue[TaskQueueSize]= Idle;				// � ��������� ������ ������ �������

			Enable_Interrupt();							// ��������� ����������
			
			(GoToTask)();								// ��������� � ������
			
		}
}


/*
������ �������� ����. ������ ���������� �� ���������� ��� � 1��. ���� ����� ����� ����������� � ����������� �� ������

To DO: �������� � ����������� ��������� ������������ ������� ��������. ����� ����� ����� ��������� �� ����� ������. 
� ����� ������������ ��� ������� ������������ �������. 
� ���� ������ �� ������ �������� �������� ����������. 
*/
inline void TimerService(void)	{
	uint8_t index;

	for(index=0;index!=MainTimerQueueSize+1;index++)		// ����������� ������� ��������
		{
		if(MainTimer[index].GoToTask == Idle) continue;		// ���� ����� �������� - ������� ��������� ��������
		if(MainTimer[index].Time !=1)						// ���� ������ �� ��������, �� ������� ��� ���. 
			{												// To Do: ��������� �� ������, ��� ����� !=1 ��� !=0. 
				MainTimer[index].Time --;						// ��������� ����� � ������ ���� �� �����.
			}
		else {
				SetTask(MainTimer[index].GoToTask);				// ��������� �� ����? ������ � ������� ������
				MainTimer[index].GoToTask = Idle;				// � � ������ ����� �������
			}
		}
}


