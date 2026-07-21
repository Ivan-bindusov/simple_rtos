#ifndef RTOS_CORE_H
#define RTOS_CORE_H

#include <stdint.h> 
#include <stddef.h>
#include "stm32f411xe.h"

extern volatile uint8_t os_running;

#define SYS_TICK_LOAD 100000000U
#define MAX_TASKS   5
#define STACK_SIZE  256
#define STACK_CANARY_WORD 0xDEADBEEF

typedef struct {
    void *sp;               // Текущий указатель стека задачи
    uint32_t ticksToWait;   // Счетчик ожидания
    uint8_t priority;       // Текущий рабочий приоритет задачи
    uint8_t basePriority;   // Базовый (начальный) уровень приориета
    uint8_t is_active;      // Флаг валидности задачи, чтобы не выполнять не инициализированную задачу
} __attribute__((aligned(16))) TCB_t;

//глобальные переменные ядра
extern TCB_t TCBs[MAX_TASKS];    // Массив всех задач
extern volatile uint32_t currentTask;  // Индекс активной задачи
extern uint32_t TaskStacks[MAX_TASKS][STACK_SIZE];

void Clock_Init(void);
void Task_Create(uint8_t taskIndex, uint8_t priority, void (*taskFunc)(void));
void OS_Start(void (*firstTaskFunc)(void));
uint32_t* os_scheduler(void);
void OS_Delay(uint32_t ms);
void SysTick_Init(uint32_t load);
void OS_StackOverflow_Handler(uint32_t brokenTaskIndex);
void rtos_assert_failed(const uint8_t* file, uint32_t line);

#define RTOS_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            rtos_assert_failed((const uint8_t*)__FILE__, __LINE__); \
        } \
    } while(0)

#endif // RTOS_CORE_H