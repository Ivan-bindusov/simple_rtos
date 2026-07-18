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
    volatile uint32_t dummy = RCC->APB2ENR; (void)dummy;

    for(volatile int i = 0; i < 100; i++) { __NOP(); }

    // Настраиваем PA4 как обычный выход GPIO (для ручного управления CS)
    GPIOA->MODER &= ~GPIO_MODER_MODER4;
    GPIOA->MODER |= GPIO_MODER_MODER4_0; // General purpose output
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT4;   // Push-pull
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR4;
    //GPIOA->PUPDR |= GPIO_PUPDR_PUPDR4_0; // Pull-up
	GPIOA->BSRR = GPIO_BSRR_BS4;

	// Конфигурация регистра SPI1->CR1
    // Базовая частота SPI1 на Black Pill = 100 МГц / 2 = 50 МГц.
    SPI1->CR1 = 0;
    SPI1->CR1 = SPI_CR1_MSTR |        // STM32 — Мастер шины
                SPI_CR1_SSM  |        // Программное управление CS
                SPI_CR1_SSI  |        // Внутренний сигнал CS в режиме
                SPI_CR1_CPOL |
                SPI_CR1_CPHA |
                (3 << SPI_CR1_BR_Pos);
                
    SPI1->CR1 |= SPI_CR1_SPE;

    GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);

	GPIOA->AFR[0] &= ~((0x0F << GPIO_AFRL_AFSEL5_Pos) | 
                       (0x0F << GPIO_AFRL_AFSEL6_Pos) | 
                       (0x0F << GPIO_AFRL_AFSEL7_Pos));
    
    GPIOA->AFR[0] |= (5 << GPIO_AFRL_AFSEL5_Pos) | 
                     (5 << GPIO_AFRL_AFSEL6_Pos) | 
                     (5 << GPIO_AFRL_AFSEL7_Pos);

    // Настройка скорости, обязательно!
    GPIOA->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR5 | GPIO_OSPEEDER_OSPEEDR6 | GPIO_OSPEEDER_OSPEEDR7);
    GPIOA->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR5_0 | GPIO_OSPEEDER_OSPEEDR6_0 | GPIO_OSPEEDER_OSPEEDR7_0);

    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR5 | GPIO_PUPDR_PUPDR6 | GPIO_PUPDR_PUPDR7);
    //GPIOA->PUPDR |= (GPIO_PUPDR_PUPDR5_0 | GPIO_PUPDR_PUPDR6_0 | GPIO_PUPDR_PUPDR7_0);
    
    // Даем флешке Puya короткую паузу
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
}

uint8_t W25Q64_ReadStatus(void) {
    uint8_t status = 0;

    W25Q64_CS_Low();
    SPI1_Transfer(CMD_READ_STATUS_REG1);
    status = SPI1_Transfer(0x00);
    W25Q64_CS_High();

    return status;
}

void W25Q64_WaitReady(void) {
    while (W25Q64_ReadStatus() & 0x01) {
        OS_Delay(1);
    }
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

void W25Q64_Read(uint32_t addr, uint8_t* buf, uint32_t len) {
    if (len == 0 || buf == NULL) return;

    W25Q64_WaitReady();

    W25Q64_CS_Low();
    // Небольшая задержка после CS
    for(volatile int i = 0; i < 5; i++) { __NOP(); }

    SPI1_Transfer(CMD_READ_DATA);
    
    SPI1_Transfer((uint8_t)((addr >> 16) & 0xFF)); // Старший байт адреса
    SPI1_Transfer((uint8_t)((addr >> 8) & 0xFF)); // Средний байт адреса
    SPI1_Transfer((uint8_t)(addr & 0xFF)); // Младший байт адреса

    // 2. Выкачиваем байты из флешки один за другим
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = SPI1_Transfer(0x00); // Шлем фиктивный байт, забираем данные
    }
    
    W25Q64_CS_High(); // Отпускаем флешку
}

void W25Q64_WritePage(uint32_t addr, uint8_t* buf, uint32_t len) {
    if (len == 0 || buf == NULL || len > 256) return;

    // 1. Снимаем защиту записи
    W25Q64_CS_Low();
    SPI1_Transfer(CMD_WRITE_ENABLE);
    W25Q64_CS_High();

    // Короткая микросекундная пауза, чтобы флешка успела взвести флаг WEL
    for(volatile int i = 0; i < 50; i++);

    // 2. Отправляем команду записи страницы и адрес
    W25Q64_CS_Low();
    SPI1_Transfer(CMD_PAGE_PROGRAM);

    SPI1_Transfer((uint8_t)((addr >> 16) & 0xFF)); // Старший байт адреса
    SPI1_Transfer((uint8_t)((addr >> 8) & 0xFF)); // Средний байт адреса
    SPI1_Transfer((uint8_t)(addr & 0xFF)); // Младший байт адреса

    // 3. Заталкиваем байты данных в буфер флешки
    for (uint32_t i = 0; i < len; i++) {
        SPI1_Transfer(buf[i]);
    }
    W25Q64_CS_High();

    W25Q64_WaitReady();
}

// Функция стирания сектора (4 Килобайта) — обязательна перед записью в чистый сектор!
void W25Q64_EraseSector(uint32_t addr) {
    W25Q64_CS_Low();
    SPI1_Transfer(CMD_WRITE_ENABLE);
    W25Q64_CS_High();
    
    for(volatile int i = 0; i < 50; i++);

    W25Q64_CS_Low();
    SPI1_Transfer(CMD_SECTOR_ERASE_4K);

    SPI1_Transfer((uint8_t)((addr >> 16) & 0xFF)); // Старший байт адреса
    SPI1_Transfer((uint8_t)((addr >> 8) & 0xFF)); // Средний байт адреса
    SPI1_Transfer((uint8_t)(addr & 0xFF)); // Младший байт адреса
    W25Q64_CS_High();

    W25Q64_WaitReady();
}