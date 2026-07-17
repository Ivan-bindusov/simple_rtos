#ifndef W25Q64_H
#define W25Q64_H

#include <stdint.h>
#include "stm32f411xe.h"

// Команды управления флеш-памятью Winbond
#define CMD_WRITE_ENABLE      0x06
#define CMD_WRITE_DISABLE     0x04
#define CMD_READ_STATUS_REG1  0x05
#define CMD_PAGE_PROGRAM      0x02  // Запись страницы (256 байт)
#define CMD_SECTOR_ERASE_4K   0x20  // Стирание сектора (4 КБ)
#define CMD_READ_DATA         0x03  // Чтение данных
#define CMD_RELEASE_POWERDOWN 0xAB  // Пробуждение чипа
#define CMD_MANUFACTURER_ID   0x90  // Чтение ID чипа (для проверки связи)

void W25Q64_Init(void);
uint16_t W25Q64_ReadID(void);
void W25Q64_Read(uint32_t addr, uint8_t* buf, uint32_t len);
void W25Q64_WritePage(uint32_t addr, uint8_t* buf, uint32_t len);
void W25Q64_EraseSector(uint32_t addr);

#endif