#include "uart.h"

void UART2_Init(uint32_t baudrate) {
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	volatile uint32_t dummy = RCC->APB1ENR; (void)dummy;

	GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |= (GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1);

	GPIOA->AFR[0] &= ~((0x0F << GPIO_AFRL_AFSEL2_Pos) | (0x0F << GPIO_AFRL_AFSEL3_Pos));
    GPIOA->AFR[0] |= (7 << GPIO_AFRL_AFSEL2_Pos) | (7 << GPIO_AFRL_AFSEL3_Pos);

	if (baudrate == 115200) {
        USART2->BRR = 0x1B2;
    } else {
        // Упрощенный расчет для других скоростей, если понадобятся
        USART2->BRR = 50000000 / baudrate;
    }

	// Включаем передатчик (TE), приемник (RE) и сам модуль USART2 (UE)
    USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void UART2_SendChar(char ch) {
	while (!(USART2->SR & USART_SR_TXE));

	USART2->DR = ch;
}

void UART2_SendString(const char* str) {
	while (*str) {
		UART2_SendChar(*str++);
	}
}

static char hex_to_char(uint8_t nibble) {
	if (nibble < 10) {
		return '0' + nibble;
	}
	return 'A' + (nibble - 10);
}

void UART2_SendHex16(uint16_t value) {
	UART2_SendString("0x");

	UART2_SendChar(hex_to_char((value >> 12) & 0x0F));
	UART2_SendChar(hex_to_char((value >> 8) & 0x0F));
	UART2_SendChar(hex_to_char((value >> 4) & 0x0F));
	UART2_SendChar(hex_to_char(value & 0x0F));
}

void UART2_SendDec(uint32_t value) {
	char buf[10];
	int i = 0;

	if (value == 0) {
		UART2_SendChar('0');
		return;
	}

	while (value > 0) {
		buf[i++] = '0' + (value % 10);
		value /= 10;
	}

	while (i > 0) {
		UART2_SendChar(buf[--i]);
	}
}

void UART2_SendInt(int32_t value) {
	char buf[11];
	int i = 0;

	if (value == 0) {
		UART2_SendChar('0');
		return;
	}

	// Если число отрицательное, выводим минус и делаем число положительным
    if (value < 0) {
        UART2_SendChar('-');
        value = -value; 
    }

	while (value > 0) {
		buf[i++] = '0' + (value % 10);
		value /= 10;
	}

	while (i > 0) {
		UART2_SendChar(buf[--i]);
	}
}