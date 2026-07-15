#include "rtos_queue.h"
#include "../core/rtos_core.h"

void Queue_Init(Queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

uint8_t Queue_Send(Queue_t *q, uint32_t data) {
    while(1) {
        __asm volatile ("cpsid i");

        if(q->count < QUEUE_SIZE) {
            //место есть, записываем данные
            q->buffer[q->tail] = data;
            q->tail = (q->tail + 1) % QUEUE_SIZE;
            q->count++;

            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

            __asm volatile ("cpsie i");
            return 1;
        } else {
            TCBs[currentTask].ticksToWait = 1; //отправляем задачу в спящий режим
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

            __asm volatile ("cpsie i");
            __asm volatile ("isb");
        }
    }
}

uint8_t Queue_Receive(Queue_t *q, uint32_t *data) {
    while(1) {
        __asm volatile ("cpsid i");

        if(q->count > 0) {
            *data = q->buffer[q->head];
            q->head = (q->head + 1) % QUEUE_SIZE;
            q->count--;

            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

            __asm volatile ("cpsie i");
            return 1;
        } else {
            //очередь пуста, отправляем задачу в спящий режим
            TCBs[currentTask].ticksToWait = 1;
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

            __asm volatile ("cpsie i");
            __asm volatile ("isb");
        }
    }
}