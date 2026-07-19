#include "rtos_spi.h"
#include <stddef.h>

// Создаем массив управляющих структур под все 3 аппаратных модуля SPI в STM32F411
static RTOS_SPI_Handle_t spiHandles[RTOS_SPI_MAX] = {
    {SPI1, {0}, 0},
    {SPI2, {0}, 0},
    {SPI3, {0}, 0}
};

void RTOS_SPI_Init(RTOS_SPI_Num_t spiNum, RTOS_SPI_Mode_t mode, RTOS_SPI_Baud_t baud) {
	if (spiNum >= RTOS_SPI_MAX) return;

	RTOS_SPI_Handle_t* hSpi = &spiHandles[spiNum];

	// 1. Инициализируем наш ОСРВ-мьютекс для защиты этого модуля SPI
	Mutex_Init(&hSpi->busMutex);

	// 2. Аппаратно включаем тактирование шин и настраиваем GPIO в зависимости от модуля
    if (spiNum == RTOS_SPI_1) {
        // SPI1 сидит на высокоскоростной шине APB2 (100 МГц)
        RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
        volatile uint32_t dummy = RCC->APB2ENR; (void)dummy; // Даем шине стабилизировать ток
        
        // Включаем тактирование портов GPIOA
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        
        // Переводим PA5 (SCK), PA6 (MISO), PA7 (MOSI) в режим Альтернативной Функции (10 - AF mode)
        GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
        GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);
        
        // Переводим ножки в High Speed режим (0x10 или 0x11 в OSPEEDR)
        // Взводим 0-й бит маски скорости для пинов 5, 6, 7 — выставляем высокую скорость
        GPIOA->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR5_0 | GPIO_OSPEEDER_OSPEEDR6_0 | GPIO_OSPEEDER_OSPEEDR7_0);
        
        // Чистим и жестко записываем AF5 (SPI1) в регистр альтернативных функций AFR[0] (он же AFRL)
        GPIOA->AFR[0] &= ~((0x0F << GPIO_AFRL_AFSEL5_Pos) | (0x0F << GPIO_AFRL_AFSEL6_Pos) | (0x0F << GPIO_AFRL_AFSEL7_Pos));
        GPIOA->AFR[0] |= (5 << GPIO_AFRL_AFSEL5_Pos) | (5 << GPIO_AFRL_AFSEL6_Pos) | (5 << GPIO_AFRL_AFSEL7_Pos);
        
        // Полностью отключаем внутренние подтяжки, чтобы сигналы переключались без емкостных тормозов
        GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR5 | GPIO_PUPDR_PUPDR6 | GPIO_PUPDR_PUPDR7);
    }

	// 3. НАСТРОЙКА УПРАВЛЯЮЩЕГО РЕГИСТРА CR1 (Control Register 1)
    SPI_TypeDef* SPIx = hSpi->regs;
    SPIx->CR1 = 0; // Полностью обнуляем конфигурацию перед записью
    
    // Настраиваем базовые биты архитектуры SPI:
    SPIx->CR1 |= SPI_CR1_MSTR; // Устанавливаем STM32 в режим Мастера (Master configuration)
    SPIx->CR1 |= SPI_CR1_SSM;  // Включаем Software Slave Management (Программное управление CS)
    SPIx->CR1 |= SPI_CR1_SSI;  // Internal Slave Select = 1. Защищает Мастера от падения в ошибку MODF
    
    // Записываем делитель частоты в биты BR[2:0] (Baud Rate Selection)
    SPIx->CR1 |= (baud << SPI_CR1_BR_Pos);
    
    // Настраиваем фазу и полярность тактового сигнала (SPI Mode)
    if (mode == RTOS_SPI_MODE_1) {
        SPIx->CR1 |= SPI_CR1_CPHA;
    } else if (mode == RTOS_SPI_MODE_2) {
        SPIx->CR1 |= SPI_CR1_CPOL;
    } else if (mode == RTOS_SPI_MODE_3) {
        SPIx->CR1 |= SPI_CR1_CPOL | SPI_CR1_CPHA; // CPOL=1 (Idle high), CPHA=1 (Capture on 2nd edge)
    }

	// 4. ВКЛЮЧЕНИЕ МОДУЛЯ В КРЕМНИИ
    SPIx->CR1 |= SPI_CR1_SPE; // SPI Enable — активируем приемопередатчик в кристалле
    
    // Холостое чтение регистров, чтобы сбросить случайные флаги ошибок на старте
    volatile uint32_t dummy_read = SPIx->DR;
    dummy_read = SPIx->SR;
    (void)dummy_read;
    
    hSpi->isOpened = 1; // Драйвер готов к работе
}

// Захват шины конкретной задачей ОСРВ (Защита от одновременного доступа)
void RTOS_SPI_LockBus(RTOS_SPI_Num_t spiNum) {
    if (spiNum < RTOS_SPI_MAX && spiHandles[spiNum].isOpened) {
        // Наш приоритетный мьютекс усыпит задачу, если шина сейчас занята другой задачей
        Mutex_Lock(&spiHandles[spiNum].busMutex, 0xFFFFFFFF); 
    }
}

// Освобождение шины
void RTOS_SPI_UnlockBus(RTOS_SPI_Num_t spiNum) {
    if (spiNum < RTOS_SPI_MAX && spiHandles[spiNum].isOpened) {
        Mutex_Unlock(&spiHandles[spiNum].busMutex);
    }
}

// Потокобезопасный приемопередающий примитив (1 байт)
uint8_t RTOS_SPI_TransferByte(RTOS_SPI_Num_t spiNum, uint8_t data) {
    SPI_TypeDef* SPIx = spiHandles[spiNum].regs;
    
    // Ждем флаг TXE (Transmit buffer empty) — регистр готов принять байт в буфер отправки
    while (!(SPIx->SR & SPI_SR_TXE));
    SPIx->DR = data; // Выстреливаем байт в шину
    
    // Ждем флаг RXNE (Receive buffer not empty) — флешка/датчик вернули ответный байт
    while (!(SPIx->SR & SPI_SR_RXNE));
    return (uint8_t)(SPIx->DR); // Чтение DR автоматически очищает флаг RXNE в железе
}

// Пакетная передача массива данных (Блокирующий режим вывода)
void RTOS_SPI_Transmit(RTOS_SPI_Num_t spiNum, const uint8_t* pTxData, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        // При пакетной отправке мы обязаны считывать ответный байт в пустоту,
        // чтобы не переполнить внутренний регистр приемника SPI (ошибка Overrun)
        (void)RTOS_SPI_TransferByte(spiNum, pTxData[i]);
    }
}

// Пакетный прием массива данных
void RTOS_SPI_Receive(RTOS_SPI_Num_t spiNum, uint8_t* pRxData, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        // Отправляем флешке пустой байт 0x00, чтобы сгенерировать такты SCK, и забираем данные
        pRxData[i] = RTOS_SPI_TransferByte(spiNum, 0x00);
    }
}