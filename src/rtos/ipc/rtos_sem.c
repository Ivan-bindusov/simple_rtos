#include "rtos_sem.h"
#include "../core/rtos_core.h"

void Semaphore_Init(Semaphore_t *sem, uint32_t initialCount, uint32_t maxCount) {
    sem->count = initialCount;
    sem->maxCount = maxCount;
    sem->blockedTasks = 0;
}

uint8_t Semaphore_Take(Semaphore_t *sem, uint32_t timeout) {
    __asm volatile ("cpsid i");

    if (sem->count > 0) {
        sem->count--; // Забираем одно разрешение
        __asm volatile ("cpsie i");
        return 1;
    }

    // Если разрешений нет, а таймаут равен нулю - уходим ни с чем
    if (timeout == 0) {
        __asm volatile("cpsie i");
        return 0;
    }

    // Блокируем текущую задачу
    sem->blockedTasks |= (1 << currentTask);
    TCBs[currentTask].ticksToWait = timeout;

    __asm volatile ("cpsie i");

    // Актвируем планировщик, чтобы переключить процессор на другую задачу
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    __ISB();

    // (sem->blockedTasks & (1 << currentTask)) == 0 - значит что задачу "разбудили"
    __asm volatile ("cpsid i");
    if ((sem->blockedTasks & (1 << currentTask)) == 0) {
        __asm volatile("cpsie i");
        return 1; // Задача разблокирована
    }

    // Если проснулись по таймауту, просто забираем себя из списка ожидания
    sem->blockedTasks &= ~(1 << currentTask);
    __asm volatile ("cpsie i");
    return 0; // Сбой по таймауту
}

// Освобождение семафора (выстрел сигнальной ракеты)
void Semaphore_Give(Semaphore_t *sem) {
    __asm volatile("cpsid i");

    // Если ктото ожидает этот семафор
    if (sem->blockedTasks != 0) {
        int8_t highPriorityTaskIndex = -1;
        uint8_t highPriorityTaskValue = 255;

        // Ищем задачу наивысшего приоритета, среди тех, которые стоят в ожидании семафора
        for (uint8_t i=0;i<MAX_TASKS;i++) {
            if (sem->blockedTasks & (1 << i)) {
                if (TCBs[i].priority < highPriorityTaskValue) {
                    highPriorityTaskValue = TCBs[i].priority;
                    highPriorityTaskIndex = i;
                }
            }
        }

        if (highPriorityTaskIndex != -1) {
            // Пробуждаем задачу
            sem->blockedTasks &= ~(1 << highPriorityTaskIndex);
            TCBs[highPriorityTaskIndex].ticksToWait = 0;

            __asm volatile ("cpsie i");

            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
            return;
        }
    }

    // Если ни кто не ждал, увеличиваем счетчик - стандартный "накопленный" режим семафора
    if (sem->count < sem->maxCount) {
        sem->count++;
    }

    __asm volatile ("cpsie i");
}