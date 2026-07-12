#include <stdint.h>
#include "stm32f4xx.h"
#include "rtos/rtos.h"

Semaphore_t testSemaphore;

void Task1_Medium(void) {
    OS_Delay(10);

    if (Semaphore_Take(&testSemaphore, 0xFFFFFFFF)) {
        while(1) {
            OS_Delay(100);
        }
    }
}

void Task2_High(void) {
    OS_Delay(10);

    if (Semaphore_Take(&testSemaphore, 0xFFFFFFFF)) {
        while(1) {
            OS_Delay(200);
        }
    }
}

void Task3_Signaler(void) {
    OS_Delay(50);

    Semaphore_Give(&testSemaphore);
    while(1) {
        OS_Delay(10);
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

    // Разрешаем отладку во время сна WFI
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
    // 1. АППАРАТНОЕ ВКЛЮЧЕНИЕ FPU (Разрешаем полный доступ к сопроцессорам CP10 и CP11)
    // Без этого шага любая инструкция float вызовет моментальный HardFault!
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

    OS_Timer_Create(0, 500, 1, Led_Timer_Callback);
    OS_Timer_Start(0);

    Semaphore_Init(&testSemaphore, 0, 1);

    Task_Create(0, 10, Task1_Medium);
    Task_Create(1, 2, Task2_High);
    Task_Create(2, 5, Task3_Signaler);
    Task_Create(4, 255, Task4_Idle);

    SysTick_Init(100000000 / 1000);

    OS_Start(Task1_Medium);

    while(1);
}