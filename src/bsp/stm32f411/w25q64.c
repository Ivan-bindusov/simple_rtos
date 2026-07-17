#include "w25q64.h"
#include "rtos.h"

// Управление ножкой CS (PA4)
static inline void W25Q64_CS_Low(void) { GPIOA->BSRR = GPIO_BSRR_BR4; }
static inline void W25Q64_CS_High(void) { GPIOA->BSRR = GPIO_BSRR_BS4; }

// Приемопередача 1 байта по SPI
static uint8_t SPI1_Transfer(uint8_t data) {
	while (!(SPI1->SR & SPI_SR_TXE));

	SPI1->DR = data;

	while (!(SPI1->SR & SPI_SR_RXNE));

	return (uint8_t)SPI1->DR;
}

void W25Q64_Init(void) {
	// 1. Включаем тактирование портов GPIOA и модуля SPI1
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR; // Короткая задержка на включение периферии

	SPI1->CR1 = 0;

	// Конфигурация регистра SPI1->CR1
    // Базовая частота SPI1 на Black Pill = 100 МГц / 2 = 50 МГц.
    SPI1->CR1 = SPI_CR1_MSTR |        // STM32 — Мастер шины
                SPI_CR1_SSM  |        // Программное управление CS
                SPI_CR1_SSI  |        // Внутренний сигнал CS в режиме
				//SPI_CR1_CPOL |        
                //SPI_CR1_CPHA |
                (6 << SPI_CR1_BR_Pos);
                
    SPI1->CR1 |= SPI_CR1_SPE;

     GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);

	GPIOA->AFR[0] &= ~((0x0F << GPIO_AFRL_AFSEL5_Pos) | 
                       (0x0F << GPIO_AFRL_AFSEL6_Pos) | 
                       (0x0F << GPIO_AFRL_AFSEL7_Pos));
    
    GPIOA->AFR[0] |= (5 << GPIO_AFRL_AFSEL5_Pos) | 
                     (5 << GPIO_AFRL_AFSEL6_Pos) | 
                     (5 << GPIO_AFRL_AFSEL7_Pos);

    // Настраиваем PA4 как обычный выход GPIO (для ручного управления CS)
	GPIOA->BSRR = GPIO_BSRR_BS4;
    GPIOA->MODER &= ~GPIO_MODER_MODER4;
    GPIOA->MODER |= GPIO_MODER_MODER4_0; // General purpose output

    //GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR6);
    // Выставляем значение 01 — Pull-Up (подтяжка к 3.3V)
    //GPIOA->PUPDR |= GPIO_PUPDR_PUPDR6_0; 

	// 5. ИСПРАВЛЕНИЕ ДЛЯ PUYA: Будим флешку из глубокого сна (Release from Deep Power-Down)
    W25Q64_CS_Low();
    SPI1_Transfer(CMD_RELEASE_POWERDOWN); // Шлем команду 0xAB
    W25Q64_CS_High();
    
    // Даем флешке Puya короткую паузу (около 30 микросекунд), чтобы её логика проснулась
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
}

// Функция проверки связи: читает ID производителя флешки (должно вернуться 0xEF16)
uint16_t W25Q64_ReadID(void) {
    uint16_t id = 0;
    W25Q64_CS_Low();
    
    SPI1_Transfer(CMD_MANUFACTURER_ID);
    SPI1_Transfer(0x00); // 3 фиктивных байта адреса по даташиту
    SPI1_Transfer(0x00);
    SPI1_Transfer(0x00);
    
    id |= (SPI1_Transfer(0x00) << 8); // Считываем Manufacturer ID (0xEF)
    id |= SPI1_Transfer(0x00);        // Считываем Device ID (0x16)
    
    W25Q64_CS_High();
    return id;
}