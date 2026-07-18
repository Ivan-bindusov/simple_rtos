#ifndef RTOS_LOG_H
#define RTOS_LOG_H

#include <stdint.h>

#define LOG_SECTOR_START 0
#define LOG_SECTOR_COUNT 16
#define LOG_SECTOR_SIZE 4096
#define LOG_PAGE_SIZE 256
#define LOG_MAGIC_BYTE 0xA5

// Структура одного сообщения лога
typedef struct {
	uint8_t magic;
	uint32_t timestamp;
	uint8_t taskIndex;
	char message[246]; // 256 - 10 (magic - 1, timestamp - 4, taskIndex - 1, crc - 4)
	uint32_t crc; // Простая контрольная сумма для валидации данных
} __attribute__((packed)) LogEntry_t;

void OS_Log_Init(void);
void OS_Log_Write(uint8_t taskIdx, const char* msg);
void OS_Log_DumpToUART(void); // Вывод всех накопленных логов в UART

#endif