#include <stdint.h>
#include "stm32f4xx.h"
#include "rtos/rtos.h"

void Task1_HeapTest(void) {
    OS_Delay(10);

    if (rtosHeap.flBitmap == 0) {
        while(1) {
            // Быстрое аварийное мигание: куча вообще не инициализировалась!
            //GPIOC->ODR ^= GPIO_ODR_ODR_13; 
            for(volatile int i=0; i<1000000; i++);
        }
    }

    while(1) {
        // Выделяем память
        void* a = OS_Malloc(32);
        void* b = OS_Malloc(64);
        void* c = OS_Malloc(128);

        // Если хоть один вызов вернул NULL (ошибку) — куча сломалась на старте
        if (a == NULL || b == NULL || c == NULL) {
            while(1) {
                //GPIOC->ODR ^= GPIO_ODR_ODR_13; 
                for(volatile int i=0; i<10000000; i++);
            }
        }

        // Освобождаем куски
        OS_Free(b); 
        OS_Free(a);
        OS_Free(c);

        void* d = OS_Malloc(4096);

        if (d != NULL) {
            // ПОБЕДА! Куча работает идеально, слияние произошло!
            // В знак этого инвертируем светодиод PC13
            GPIOC->ODR ^= GPIO_ODR_ODR_13; 
            
            OS_Free(d); // Очищаем кучу для следующего круга
        } else {
            //GPIOC->ODR &= ~GPIO_ODR_ODR_13;
            while(1);
        }

        OS_Delay(500);
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

    OS_Heap_Init();

    Task_Create(0, 10, Task1_HeapTest);
    Task_Create(4, 255, Task4_Idle);

    SysTick_Init(100000000 / 1000);

    OS_Start(Task1_HeapTest);

    while(1);
}