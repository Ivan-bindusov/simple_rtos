#ifndef RTOS_SEM_H
#define RTOS_SEM_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f411xe.h"

typedef struct {
    uint32_t count;  // Текущее значение счетчика разрешений
    uint32_t maxCount;  // Максимальный предел (1 для бинарного, >1 для счетного)
    uint8_t blockedTasks;   // Битовая маска заблокированных задач
} Semaphore_t;

void Semaphore_Init(Semaphore_t *sem, uint32_t initialCount, uint32_t maxCount);
uint8_t Semaphore_Take(Semaphore_t *sem, uint32_t timeout); // Взять разрешение
void Semaphore_Give(Semaphore_t *sem); // Вернуть разрешение

#endif