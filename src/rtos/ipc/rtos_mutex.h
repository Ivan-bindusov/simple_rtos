#ifndef RTOS_MUTEX_H
#define RTOS_MUTEX_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f411xe.h"

typedef struct {
    volatile uint8_t locked;
    volatile uint8_t ownerTask;
    volatile uint32_t blockedTasks;  // Битовая маска заблокированных по мьютексам задач
} Mutex_t;

//функции для работы с мьютексами
void Mutex_Init(Mutex_t *mutex);
uint8_t Mutex_Lock(Mutex_t *mutex, uint32_t timeout);
void Mutex_Unlock(Mutex_t *mutex);

#endif