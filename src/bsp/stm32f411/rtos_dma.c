#include "rtos_dma.h"
#include "rtos.h"
//#include "../rtos/core/rtos_core.h"
#include "rtos_spi.h"

Semaphore_t dmaSpi1RxSem;

void RTOS_DMA_Init(void) {
	RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
	volatile uint32_t dummy = RCC->AHB1ENR; (void)dummy;

	Semaphore_Init(&dmaSpi1RxSem, 0, 1);

	DMA2_Stream0->CR = 0;
	while(DMA2_Stream0->CR & DMA_SxCR_EN);

	DMA2_Stream0->CR |= (3 << DMA_SxCR_CHSEL_Pos); // Выбираем канал 3 (SPI1_RX)
	DMA2_Stream0->CR |= (0 << DMA_SxCR_DIR_Pos); // Направление 00 - periferal to memory

	DMA2_Stream0->CR |= (DMA_SxCR_MINC); // Адрес в RAM на шаг вперед
	DMA2_Stream0->CR &= ~(DMA_SxCR_PINC); // Адрес периферии не инкрементируется

	DMA2_Stream0->CR |= (0 << DMA_SxCR_MSIZE_Pos); // Размер в памяти: 8 бит
	DMA2_Stream0->CR |= (0 << DMA_SxCR_PSIZE_Pos); // Размер в периферии: 8 бит

	DMA2_Stream0->CR |= (3 << DMA_SxCR_PL_Pos); // Приоритет аппаратного арбитража

	DMA2_Stream0->CR |= DMA_SxCR_TCIE; // Transfer complete interrupt

	NVIC_SetPriority(DMA2_Stream0_IRQn, 6);
	NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

void RTOS_SPI1_Receive_DMA(uint8_t* pRxBuffer, uint32_t size) {
	if (size == 0 || pRxBuffer == NULL) return;

	RTOS_ASSERT(os_running == 0 || spiHandles[RTOS_SPI_1].busMutex.ownerTask == currentTask);

	// Очищаем старые флаги прерываний
	DMA2->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;

	// Указываем адреса и длину перекачки в аппаратные регистры стрима
	DMA2_Stream0->PAR = (uint32_t)&(SPI1->DR); // АДРЕС источника данных - DR spi1
	DMA2_Stream0->M0AR = (uint32_t)pRxBuffer; // Приемник данных - адрес буффера в RAM
	DMA2_Stream0->NDTR = size; // Количество байт, которое нужно перенести

	SPI1->CR2 |= SPI_CR2_RXDMAEN; // Сообщаем модулю spi, что он должен отдавать данные в контроллер DMA

	DMA2_Stream0->CR |= DMA_SxCR_EN; // Запускаем движок DMA2

	Semaphore_Take(&dmaSpi1RxSem, 0xFFFFFFFF); // Задача засыпает

	SPI1->CR2 &= ~SPI_CR2_RXDMAEN; // Когда задача проснется (по семафору), выключаем запрос DMA в SPI
}

// Аппаратный обработчит прерывания DMA2
void DMA2_Stream0_IRQHandler(void) {
	if (DMA2->LISR & DMA_LISR_TCIF0) { // Проверяем, что прерывание произошло по окончании перекачки
		DMA2->LIFCR = DMA_LIFCR_CTCIF0; // Очищаем флаг прерывания

		Semaphore_Give(&dmaSpi1RxSem); // Выдаем семафор - сообщаем ядру, что данные уже в RAM

		SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Для переключения на проснувшуюся задачу
	}
}