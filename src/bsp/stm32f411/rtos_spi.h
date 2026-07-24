#ifndef RTOS_SPI_H
#define RTOS_SPI_H

#include <stdint.h>
#include "stm32f411xe.h"
#include "ipc/rtos_mutex.h"

typedef enum {
	RTOS_SPI_1 = 0,
	RTOS_SPI_2,
	RTOS_SPI_3,
	RTOS_SPI_MAX,
} RTOS_SPI_Num_t;

// Перечисление режимов работы SPI
typedef enum {
	RTOS_SPI_MODE_0 = 0, // CPOL = 0, CPHA = 0 (Чтение по 1-му фронту, пассивный 0)
	RTOS_SPI_MODE_1,	// CPOL = 0, CPHA = 1 (Чтение по 2-му фронту, пассивный 0)
	RTOS_SPI_MODE_2,	// CPOL = 1, CPHA = 0 (Чтение по 1-му фронту, пассивный 1)
	RTOS_SPI_MODE_3,	// CPOL = 1, CPHA = 1 (Чтение по 2-му фронту, пассивный 1)
} RTOS_SPI_Mode_t;

// Делители частоты тактирования шины SPI (Baud Rate)
// Частота SPI = Частота шины APB / Делитель
typedef enum {
    RTOS_SPI_BAUD_DIV_2   = 0,
    RTOS_SPI_BAUD_DIV_4   = 1,
    RTOS_SPI_BAUD_DIV_8   = 2,
    RTOS_SPI_BAUD_DIV_16  = 3,
    RTOS_SPI_BAUD_DIV_32  = 4,
    RTOS_SPI_BAUD_DIV_64  = 5,
    RTOS_SPI_BAUD_DIV_128 = 6,
    RTOS_SPI_BAUD_DIV_256 = 7
} RTOS_SPI_Baud_t;

// Структура управления конкретным аппаратным интерфейсом SPI
typedef struct {
    SPI_TypeDef*    regs;      // Указатель на карту регистров STM32 (SPI1, SPI2 или SPI3)
    Mutex_t         busMutex;  // Мьютекс ОСРВ для защиты шины от одновременного доступа задач
    uint8_t         isOpened;  // Флаг того, что интерфейс успешно инициализирован
} RTOS_SPI_Handle_t;

// Прототипы универсального API
void RTOS_SPI_Init(RTOS_SPI_Num_t spiNum, RTOS_SPI_Mode_t mode, RTOS_SPI_Baud_t baud);
void RTOS_SPI_LockBus(RTOS_SPI_Num_t spiNum);
void RTOS_SPI_UnlockBus(RTOS_SPI_Num_t spiNum);
uint8_t RTOS_SPI_TransferByte(RTOS_SPI_Num_t spiNum, uint8_t data);
void RTOS_SPI_Transmit(RTOS_SPI_Num_t spiNum, const uint8_t* pTxData, uint32_t size);
void RTOS_SPI_Receive(RTOS_SPI_Num_t spiNum, uint8_t* pRxData, uint32_t size);
void RTOS_SPI_Receive_DMA(RTOS_SPI_Num_t spiNum, uint8_t* pRxData, uint32_t size);
void RTOS_SPI_Init_Mutexes(void);

extern RTOS_SPI_Handle_t spiHandles[RTOS_SPI_MAX];

#endif