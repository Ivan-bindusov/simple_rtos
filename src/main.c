#include "rtos/rtos.h"
#include "bsp/stm32f411/w25q64.h"

#define LIS_CS_LOW() (GPIOA->BSRR |= GPIO_BSRR_BR1);
#define LIS_CS_HIGH() (GPIOA->BSRR |= GPIO_BSRR_BS1);

uint16_t idFlash;
Semaphore_t sensorDataReadySem;
Queue_t sensorQueue;

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
} LIS3DH_Data_t;

void LIS3DH_Hardware_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Настройка пина PA0 как Output для CS
    GPIOA->MODER &= ~(GPIO_MODER_MODE1);
    GPIOA->MODER |= (1 << GPIO_MODER_MODE1_Pos);
    LIS_CS_HIGH(); // Иначально чип отключен

    GPIOA->MODER &= ~GPIO_MODER_MODER0; // Mode: Input
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR0; 
    GPIOA->PUPDR |= (2 << GPIO_PUPDR_PUPD0_Pos);

    // Настройка прерывания от датчика
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0;

    EXTI->IMR |= EXTI_IMR_IM0; // Разрешаем прерывание EXTI0
    EXTI->RTSR |= EXTI_RTSR_TR0; // Срабатывание по переднему фронту

    RTOS_SPI_LockBus(RTOS_SPI_1);
    LIS_CS_LOW();
    (void)RTOS_SPI_TransferByte(RTOS_SPI_1, 0xE8);
    for(int i = 0; i < 6; i++) {
        RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00); // Вычитываем 6 байт в пустоту
    }
    LIS_CS_HIGH();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);

    EXTI->PR = EXTI_PR_PR0; 

    NVIC_SetPriority(EXTI0_IRQn, 6);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void LIS3DH_WriteReg(uint8_t reg, uint8_t value) {
    RTOS_SPI_LockBus(RTOS_SPI_1);
    LIS_CS_LOW();
    uint8_t reg_val[2];
    reg_val[0] = reg & 0x7F;
    reg_val[1] = value;
    RTOS_SPI_Transmit(RTOS_SPI_1, reg_val, 2);
    LIS_CS_HIGH();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);
}

uint8_t LIS3DH_ReadReg(uint8_t reg) {
    RTOS_SPI_LockBus(RTOS_SPI_1);
    uint8_t val;
    LIS_CS_LOW();
    (void)RTOS_SPI_TransferByte(RTOS_SPI_1, reg | 0x80);
    val = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
    LIS_CS_HIGH();
    RTOS_SPI_UnlockBus(RTOS_SPI_1);
    return val;
}

void EXTI0_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR0) {
        EXTI->PR = EXTI_PR_PR0;

        Semaphore_Give(&sensorDataReadySem);
    }
}

void LIS3DH_Init(void) {
    if (LIS3DH_ReadReg(0x0F) != 0x33) {
        UART2_SendString("LIS3DH connection failed!");
        OS_Log_Write(1, "LIS3DH connection failed!");
        return;
    }
    UART2_SendString("LIS3DH connection established.");
    LIS3DH_WriteReg(0x20, 0x27); // Режим Normal/High-Res, все оси (X, Y, Z) включены
    LIS3DH_WriteReg(0x23, 0x08); // High-Resolution mode, Scale +-2g
    LIS3DH_WriteReg(0x22, 0x10); // Включить DRDY прерывание на INT1
}

void Task1_HeapTest(void) {
    OS_Delay(10);

    while(1) {
        if (Semaphore_Take(&sensorDataReadySem, 0xFFFFFFFF)) {
           
            LIS3DH_Data_t *sample = (LIS3DH_Data_t*)OS_Malloc(sizeof(LIS3DH_Data_t));
            RTOS_SPI_LockBus(RTOS_SPI_1);
            LIS_CS_LOW();
            (void)RTOS_SPI_TransferByte(RTOS_SPI_1, 0xE8);

            uint8_t xl = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
            uint8_t xh = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
            uint8_t yl = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
            uint8_t yh = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
            uint8_t zl = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);
            uint8_t zh = RTOS_SPI_TransferByte(RTOS_SPI_1, 0x00);

            LIS_CS_HIGH();
            RTOS_SPI_UnlockBus(RTOS_SPI_1);

            sample->ax = (int16_t)((xh << 8) | xl) >> 4;
            sample->ay = (int16_t)((yh << 8) | yl) >> 4;
            sample->az = (int16_t)((zh << 8) | zl) >> 4;

            if(!Queue_Send(&sensorQueue, (uint32_t)sample)) {
                OS_Free(sample);
            }
        }
    }
}

void Task_DataProcessor(void) {
    uint32_t sample;

    while(1) {

        if (Queue_Receive(&sensorQueue, &sample)) {
            LIS3DH_Data_t* data = (LIS3DH_Data_t*)sample;

            UART2_SendString("\r\n");
            UART2_SendInt(data->ax);

            OS_Free(data);
        }
    }
}

void Task_SystemInit(void) {

    RTOS_SPI_Init_Mutexes();
    RTOS_DMA_Init();

    Semaphore_Init(&sensorDataReadySem, 0, 1);
    Queue_Init(&sensorQueue);

    OS_Log_Init();
    OS_Log_DumpToUART();

    LIS3DH_Hardware_Init();
    LIS3DH_Init();

    while(1) {
        OS_Delay(0xFFFFFFFF); 
    }
}

// Фоновый холостой процесс системы (Idle)
void Task4_Idle(void) {
    while(1) {
        //__WFI(); // Сон процессора до следующего системного тика (экономия энергии)
        __NOP();
    }
}

int main(void) {
    Clock_Init();
    W25Q64_Init();
    UART2_Init(115200);
    OS_Heap_Init();

    for(volatile int i = 0; i < 500000; i++) { __NOP(); }

    // Разрешаем отладку во время сна WFI
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
    // 1. АППАРАТНОЕ ВКЛЮЧЕНИЕ FPU (Разрешаем полный доступ к сопроцессорам CP10 и CP11)
    SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));
    // 2. АКТИВАЦИЯ РЕЖИМА LAZY STACKING (Ленивое сохранение FPU-регистров)
    // Взводим биты ASPEN (авто-сохранение) и LSPEN (ленивое сохранение)
    FPU->FPCCR |= FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    GPIOC->MODER &= ~(GPIO_MODER_MODE13);
    GPIOC->MODER |= GPIO_MODER_MODER13_0; // general purpose output
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT_13); //0 = push-pull
    GPIOC->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR13);
    GPIOC->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR13_1;
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPDR13); // no pull-up or down

    GPIOC->BSRR |= GPIO_BSRR_BS_13;

    Task_Create(0, 1, Task_SystemInit);
    Task_Create(1, 10, Task1_HeapTest);
    Task_Create(2, 20, Task_DataProcessor);
    Task_Create(4, 255, Task4_Idle);

    SysTick_Init(100000000 / 1000);

    OS_Start(Task_SystemInit);

    while(1);
}