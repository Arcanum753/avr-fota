#ifndef _MOCK_ESP_INTR_ALLOC_H
#define _MOCK_ESP_INTR_ALLOC_H

// Хост-заглушка критических секций FreeRTOS (однопоточные тесты).
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux)      ((void)(mux))
#define portEXIT_CRITICAL(mux)       ((void)(mux))
#define portENTER_CRITICAL_ISR(mux)  ((void)(mux))
#define portEXIT_CRITICAL_ISR(mux)   ((void)(mux))

#endif // _MOCK_ESP_INTR_ALLOC_H
