#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32f411xe.h"

void UART2_Init(uint32_t baudrate);
void UART2_SendChar(char ch);
void UART2_SendString(const char* str);
void UART2_SendHex16(uint16_t);
void UART2_SendDec(uint32_t);

#endif