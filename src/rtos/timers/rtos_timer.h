#ifndef RTOS_TIMER_H
#define RTOS_TIMER_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f411xe.h"

#define MAX_TIMERS 4

typedef void (*TimerCallback_t)(void);

typedef struct {
    uint32_t period;
    uint32_t ticksLeft;
    uint8_t autoReload;
    uint8_t active;
    TimerCallback_t callback;
} OS_Timer_t;

void OS_Timer_Create(uint8_t id, uint32_t period, uint8_t autoReload, TimerCallback_t callback);
void OS_Timer_Start(uint8_t id);
void OS_Timer_Stop(uint8_t id);
void OS_Timers_Process(void);

#endif