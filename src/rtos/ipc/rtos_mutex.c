#include "rtos_mutex.h"
#include "../core/rtos_core.h"

void Mutex_Init(Mutex_t *mutex) {
    mutex->locked = 0;
    mutex->ownerTask = -1;
    mutex->blockedTasks = 0;
}

uint8_t Mutex_Lock(Mutex_t *mutex, uint32_t timeout) {
    while(1) {
        __asm volatile ("cpsid i");

        // Если мьютекс свободен, захватываем его
        if (mutex->locked == 0) {
            mutex->locked = 1;
            mutex->ownerTask = currentTask; // запоминаем владельца
            __asm volatile ("cpsie i");
            return 1;
        }

        // Защита от повторного захвата самим собой
        if (mutex->ownerTask == currentTask) {
            __asm volatile ("cpsie i");
            return 1;
        }

        if (timeout == 0) {
            __asm volatile ("cpsie i");
            return 0;
        }

        // Сценарий 2: Мьютекс занят. Наследование приоритета
        uint8_t ownerID = mutex->ownerTask;

        // Если приоритет текущей задачи выше, чем владельца мьютекса
        if (TCBs[currentTask].priority < TCBs[ownerID].priority) {
            // Наследуем, поднимаем приоритет слабого владельца
            TCBs[ownerID].priority = TCBs[currentTask].priority;
        }

        // Встаем в очередь ожидания
        mutex->blockedTasks |= (1 << currentTask);
        TCBs[currentTask].ticksToWait = timeout;

        __asm volatile ("cpsie i");
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        __ISB();

        // Просыпаемся (либо дали мьютекс, либо по таймауту)
        __asm volatile ("cpsid i");
        if (mutex->locked && mutex->ownerTask == currentTask) { // владение мьютексом будет передано в Mutex_Unlock
            __asm volatile ("cpsie i");
            return 1; // успешно перехватили замок
        }

        // Если проспали все попытки, то удаляем себя из очереди ожидания
        mutex->blockedTasks &= ~(1 << currentTask);
        __asm volatile ("cpsie i");
        return 0;
    }
}

void Mutex_Unlock(Mutex_t *mutex) {
    __asm volatile ("cpsid i");

    //проверяем, что данная задача владеет мьютексом
    if (mutex->ownerTask != currentTask) {
        __asm volatile ("cpsie i");
        return;
    }

    uint8_t oldOwner = currentTask;
    mutex->locked = 0;
    mutex->ownerTask= -1;

    // Сброс наследования - возвращаем текущуй задаче ее базовый приоритет
    TCBs[oldOwner].priority = TCBs[oldOwner].basePriority;

    // Проверяем ждет ли ктото этот мьютекс
    if (mutex->blockedTasks != 0) {
        int8_t highestPriorityWaitingTask = -1;
        uint8_t highestPriorityValue = 255;

        // Ищем среди ждущих, самую приоритетную
        for(uint8_t i = 0; i < MAX_TASKS; i++) {
            if (mutex->blockedTasks & (1 << i)) {
                if (TCBs[i].priority < highestPriorityValue) {
                    highestPriorityValue = TCBs[i].priority;
                    highestPriorityWaitingTask = i;
                }
            }
        }

        // Передаем замок лидеру этой очереди
        if (highestPriorityWaitingTask != -1) {
            uint8_t nextTask = highestPriorityWaitingTask;
            mutex->blockedTasks &= ~(1 << nextTask);

            mutex->locked = 1;
            mutex->ownerTask = nextTask; // Назаначаем нового владельца

            TCBs[nextTask].ticksToWait = 0; // Будим его

            __asm volatile ("cpsie i");
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
            return;
        }
    }
    __asm volatile ("cpsie i");
}