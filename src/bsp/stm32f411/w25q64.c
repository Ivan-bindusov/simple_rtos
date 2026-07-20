#include "w25q64.h"
#include "rtos.h"

// Управление ножкой CS (PA4)
static inline void W25Q64_CS_Low(void) { GPIOA->BSRR = GPIO_BSRR_BR4; }
static inline void W25Q64_CS_High(void) { GPIOA->BSRR = GPIO_BSRR_BS4; }

uint8_t spi_transfer(uint8_t data) {
    while (!(SPI1->SR & SPI_SR_TXE));

    SPI1->DR = data;

    while (!(SPI1->SR & SPI_SR_RXNE));

    return (uint8_t)(SPI1->DR);
}

void W25Q64_Init(void) {

	RTOS_SPI_Init(RTOS_SPI_1, RTOS_SPI_MODE_3, RTOS_SPI_BAUD_DIV_128);

    W25Q64_CS_High();
    
    // Даем флешке Puya короткую паузу
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
}

uint8_t W25Q64_ReadStatus(void) {
    uint8_t status = 0;
    uint8_t cmd = CMD_READ_STATUS_REG1;

    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();
    
    // Отправляем команду
    RTOS_SPI_Transmit(RTOS_SPI_1, &cmd, 1);
    // Принимаем 1 байт ответа
    RTOS_SPI_Receive(RTOS_SPI_1, &status, 1);

    W25Q64_CS_High();

    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    return status;
}

void W25Q64_WaitReady(void) {
    while (W25Q64_ReadStatus() & 0x01) {
        OS_Delay(1);
    }
}

// Функция проверки связи: читает ID производителя флешки (должно вернуться 0xEF16)
uint16_t W25Q64_ReadID(void) {
    uint8_t cmd_buf[4] = { CMD_MANUFACTURER_ID, 0x00, 0x00, 0x00 }; // Команда 0x90 + 3 байта фиктивного адреса
    uint8_t id_buf[2] = {0};
    uint16_t id = 0;
    
    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();

    // Пакетно выстреливаем команду и адрес за один вызов!
    RTOS_SPI_Transmit(RTOS_SPI_1, cmd_buf, 4);
    // Пакетно забираем 2 байта паспорта чипа
    RTOS_SPI_Receive(RTOS_SPI_1, id_buf, 2);

    W25Q64_CS_High();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    id = (id_buf[0] << 8) | id_buf[1];
    return id;
}

void W25Q64_Read(uint32_t addr, uint8_t* buf, uint32_t len) {
    if (len == 0 || buf == NULL) return;

    W25Q64_WaitReady();

    W25Q64_CS_Low();
    
    // Отправляем массив байт адреса
    uint8_t cmd_addr_buf[4];
    cmd_addr_buf[0] = CMD_READ_DATA; // 0x03
    cmd_addr_buf[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmd_addr_buf[2] = (uint8_t)((addr >> 8)  & 0xFF);
    cmd_addr_buf[3] = (uint8_t)(addr         & 0xFF);

    // Блокируем шину SPI1 для нашей текущей задачи логгера
    RTOS_SPI_LockBus(RTOS_SPI_1);

    // (void)spi_transfer(CMD_READ_DATA);
    // (void)spi_transfer(cmd_addr_buf[1]);
    // (void)spi_transfer(cmd_addr_buf[2]);
    // (void)spi_transfer(cmd_addr_buf[3]);

    RTOS_SPI_Transmit(RTOS_SPI_1, cmd_addr_buf, 4);

    // Пакетно выкачиваем данные из флешки напрямую в буфер пользователя
    RTOS_SPI_Receive(RTOS_SPI_1, buf, len);
    // for (uint8_t i=0;i<len;i++) {
    //     buf[i] = (uint8_t)spi_transfer(0x00);
    // }
    
    W25Q64_CS_High(); // Отпускаем флешку

    // Отпускаем мьютекс шины SPI1 для других задач ОСРВ
    RTOS_SPI_UnlockBus(RTOS_SPI_1);
}

void W25Q64_WritePage(uint32_t addr, uint8_t* buf, uint32_t len) {
    if (len == 0 || buf == NULL || len > 256) return;

    uint8_t cmd_en = CMD_WRITE_ENABLE; // 0x06
    uint8_t cmd_addr_buf[4];
    cmd_addr_buf[0] = CMD_PAGE_PROGRAM; // 0x02
    cmd_addr_buf[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmd_addr_buf[2] = (uint8_t)((addr >> 8)  & 0xFF);
    cmd_addr_buf[3] = (uint8_t)(addr         & 0xFF);

    // Включаем Write Enable (короткая независимая транзакция)
    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();
    RTOS_SPI_Transmit(RTOS_SPI_1, &cmd_en, 1);
    W25Q64_CS_High();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    for(volatile int i = 0; i < 50; i++); // Микропауза для установки WEL внутри флешки

    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();

    // Отправляем команду и адрес
    RTOS_SPI_Transmit(RTOS_SPI_1, cmd_addr_buf, 4);
    // Отправляем пакет байт полезных данных лога
    RTOS_SPI_Transmit(RTOS_SPI_1, buf, len);

    W25Q64_CS_High();

    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    W25Q64_WaitReady();
}

// Функция стирания сектора (4 Килобайта) — обязательна перед записью в чистый сектор!
void W25Q64_EraseSector(uint32_t addr) {
    uint8_t cmd_en = CMD_WRITE_ENABLE;
    uint8_t cmd_addr_buf[4];
    cmd_addr_buf[0] = CMD_SECTOR_ERASE_4K; // 0x20
    cmd_addr_buf[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmd_addr_buf[2] = (uint8_t)((addr >> 8)  & 0xFF);
    cmd_addr_buf[3] = (uint8_t)(addr         & 0xFF);

    // Включаем Write Enable
    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();
    RTOS_SPI_Transmit(RTOS_SPI_1, &cmd_en, 1);
    W25Q64_CS_High();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);
    
    for(volatile int i = 0; i < 50; i++);

    // Основная транзакция стирания
    RTOS_SPI_LockBus(RTOS_SPI_1);
    W25Q64_CS_Low();
    RTOS_SPI_Transmit(RTOS_SPI_1, cmd_addr_buf, 4);
    W25Q64_CS_High();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    W25Q64_WaitReady();
}