#include "rtos_timer.h"

OS_Timer_t OSTimers[MAX_TIMERS];

void OS_Timer_Create(uint8_t id, uint32_t period, uint8_t autoReload, TimerCallback_t callback) {
    if (id >= MAX_TIMERS) return;

    __asm volatile ("cpsid i");
    OSTimers[id].period = period;
    OSTimers[id].ticksLeft = period;
    OSTimers[id].autoReload = autoReload;
    OSTimers[id].callback = callback;
    OSTimers[id].active = 0;
    __asm volatile ("cpsie i");
}

void OS_Timer_Start(uint8_t id) {
    if (id >= MAX_TIMERS) return;
    __asm volatile ("cpsid i");
    OSTimers[id].ticksLeft = OSTimers[id].period;
    OSTimers[id].active = 1;
    __asm volatile ("cpsie i");
}

void OS_Timer_Stop(uint8_t id) {
    if(id >= MAX_TIMERS) return;
    OSTimers[id].active = 0;
}

void OS_Timers_Process(void) {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (OSTimers[i].active && OSTimers[i].callback) {
            if (OSTimers[i].ticksLeft > 0) {
                OSTimers[i].ticksLeft--;
            }

            if (OSTimers[i].ticksLeft == 0) {
                OSTimers[i].callback();

                if (OSTimers[i].autoReload) {
                    OSTimers[i].ticksLeft = OSTimers[i].period;
                } else {
                    OSTimers[i].active = 0;
                }
            }
        }
    }
}