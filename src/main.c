#include "rtos/rtos.h"
#include "bsp/stm32f411/w25q64.h"

volatile uint16_t flashID = 0;

void Task1_HeapTest(void) {
    OS_Delay(10);

    OS_Log_Init();

    OS_Log_DumpToUART();

    OS_Log_Write(1, "Error: Pressure detector was wrong 222");

    while(1) {
        flashID = W25Q64_ReadID();
        //UART2_SendHex16(flashID);
        OS_Delay(1000);
    }
}

// Фоновый холостой процесс системы (Idle)
void Task4_Idle(void) {
    while(1) {
        //__WFI(); // Сон процессора до следующего системного тика (экономия энергии)
        __NOP();
    }
}

void Led_Timer_Callback(void) {
    GPIOC->ODR ^= GPIO_ODR_ODR_13;
}

int main(void) {
    Clock_Init();

    for(volatile int i = 0; i < 500000; i++) { __NOP(); }

    W25Q64_Init();
    UART2_Init(115200);

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

    OS_Heap_Init();

    Task_Create(0, 10, Task1_HeapTest);
    Task_Create(4, 255, Task4_Idle);

    SysTick_Init(100000000 / 1000);

    OS_Start(Task1_HeapTest);

    while(1);
}