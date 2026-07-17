#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32f411xe.h"

void UART2_Init(uint32_t baudrate);
void UART2_SendChar(char ch);
void UART2_SendString(const char* str);

#endif