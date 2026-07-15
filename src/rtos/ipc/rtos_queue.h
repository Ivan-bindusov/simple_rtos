#ifndef RTOS_QUEUE_H
#define RTOS_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f411xe.h"

#define QUEUE_SIZE  8

typedef struct {
    uint32_t buffer[QUEUE_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint8_t count;
} Queue_t;

//функции для работы с очередями
void Queue_Init(Queue_t *q);
uint8_t Queue_Send(Queue_t *q, uint32_t data);
uint8_t Queue_Receive(Queue_t *q, uint32_t *data);

#endif