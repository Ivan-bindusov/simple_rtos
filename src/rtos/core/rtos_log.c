#include "rtos_log.h"
#include "bsp/stm32f411/w25q64.h"
#include "bsp/stm32f411/uart.h"
#include "rtos_core.h"

// Текущий глобальный адрес во флеш-памяти, куда мы будем писать следующий лог
static uint32_t current_log_addr = 0;

// Инициализация, ищем где остановились до выключения
void OS_Log_Init(void) {
	uint32_t max_addr = LOG_SECTOR_START + (LOG_SECTOR_COUNT * LOG_SECTOR_SIZE);
	uint8_t magic_check = 0;

	// Сканируем флешку постранично
	for (uint32_t addr = LOG_SECTOR_START; addr < max_addr; addr += LOG_PAGE_SIZE) {
		// Читаем только первый быйт страницы
		W25Q64_Read(addr, &magic_check, 1);

		// Если нашли 0xFF - значит страница чистая
		if (magic_check == 0xFF) {
			current_log_addr = addr;
			return;
		}
	}
	// Если лог заполнен полностью, сбрасываем в начало
	current_log_addr = LOG_SECTOR_START;
}

void OS_Log_Write(uint8_t taskIdx, const char* msg) {
	LogEntry_t entry;

	entry.magic = LOG_MAGIC_BYTE;
	entry.timestamp = 0;
	entry.taskIndex = currentTask;

	uint32_t i = 0;
	while (msg[i] != '\0' && i < 245) {
		entry.message[i] = msg[i];
		i++;
	}
	entry.message[i] = '\0';
	entry.crc = 0xAAAA; // Заглушка

	// Если текущий адрес записи попал на начало сектора (т.е. кратен 4096)
	// его надо сначала стереть
	if ((current_log_addr % LOG_SECTOR_SIZE) == 0) {
		W25Q64_EraseSector(current_log_addr);
	}

	// Прошиваем страницу
	W25Q64_WritePage(current_log_addr, (uint8_t*)&entry, sizeof(LogEntry_t));

	// Сдвигаем указатель адреса
	current_log_addr += LOG_PAGE_SIZE;

	// Если дошли до конца кольца
	uint32_t max_addr = LOG_SECTOR_START + (LOG_SECTOR_COUNT * LOG_SECTOR_SIZE);
	if (current_log_addr >= max_addr) {
		current_log_addr = LOG_SECTOR_START;
	}
}

// Вывод всей истории логов в UART
void OS_Log_DumpToUART(void) {
	uint32_t max_addr = LOG_SECTOR_START + (LOG_SECTOR_COUNT * LOG_SECTOR_SIZE);
	LogEntry_t entry;

	UART2_SendString("\r\n=== Dump black box of RTOS === \r\n");

	for (uint32_t addr = LOG_SECTOR_START; addr < max_addr; addr += LOG_PAGE_SIZE) {
		W25Q64_Read(addr, (uint8_t*)&entry, sizeof(LogEntry_t));

		// Если страница содержит маркер - выводим ее
		if (entry.magic == LOG_MAGIC_BYTE) {
			UART2_SendString("[LOG] Task: ");
            UART2_SendChar('0' + entry.taskIndex);
            UART2_SendString(" -> ");
            UART2_SendString(entry.message);
            UART2_SendString("\r\n");
		}
	}
	UART2_SendString("===============================\r\n");
}
