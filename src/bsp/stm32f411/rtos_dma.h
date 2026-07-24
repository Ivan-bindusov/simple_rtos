#ifndef RTOS_DMA_H
#define RTOS_DMA_H

#include <stdint.h>
#include "stm32f411xe.h"
#include "ipc/rtos_sem.h"

void RTOS_DMA_Init(void);
void RTOS_SPI1_Receive_DMA(uint8_t* pRxBuffer, uint32_t size);

#endif