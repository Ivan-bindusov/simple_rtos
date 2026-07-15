#include "rtos.h"
#include "rtos_port.h"

void SysTick_Init(uint32_t load) {
    SysTick->LOAD = load - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
        |SysTick_CTRL_TICKINT_Msk|SysTick_CTRL_ENABLE_Msk;

    NVIC_SetPriority(PendSV_IRQn, 15);
}

void SysTick_Handler(void) {
    for(uint8_t i=0;i<MAX_TASKS;i++) {
        if (TCBs[i].ticksToWait > 0) {
            TCBs[i].ticksToWait--;
        }
    }
    
    OS_Timers_Process();

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void __attribute((naked)) PendSV_Handler(void) {
    __asm volatile(
        "cpsid i \n"

        "mrs r0, psp \n" //указатель стека текущей задачи

        // ПРОВЕРКА ИСПОЛЬЗОВАНИЯ FPU: Проверяем 4-й бит регистра LR (EXC_RETURN)
        "tst lr, #0x10 \n"            
        "it eq \n"                    // Если бит равен 0 (задача использовала FPU)
        "vstmdbeq r0!, {s16-s31} \n"  // Сохраняем программные регистры FPU S16-S31 в её стек!

        "stmdb r0!, {r4-r11} \n" //сохраняем регистры текущей задачи

        //сохраняем указатель стека текущей задачи в ее TCB
        "ldr r1, =TCBs \n"
        "ldr r2, =currentTask \n"
        "ldr r3, [r2] \n" // r3 - индекс текущей задачи
        "lsl r3, r3, #4 \n"
        "add r1, r1, r3 \n"
        "str r0, [r1] \n"

        "ldr r1, =0xE000ED04 \n"       // Адрес регистра SCB->ICSR
        "ldr r2, =0x08000000 \n"       // Бит PENDSVCLR
        "str r2, [r1] \n"             // Сбрасываем защелку прерывания PendSV

        "push {r4, lr} \n"
        "bl os_scheduler \n" //возвращаемый указатель сохраняется в r0
        "pop {lr, r4} \n"

        "ldmia r0!, {r4-r11} \n" //восстанавливаем регистры задачи

        // ПРОВЕРКА ИСПОЛЬЗОВАНИЯ FPU ДЛЯ НОВОЙ ЗАДАЧИ
        "tst lr, #0x10 \n"            
        "it eq \n"                    // Если новая задача тоже использовала FPU
        "vldmiaeq r0!, {s16-s31} \n"  // Восстанавливаем её регистры FPU S16-S31 из её стека!

        "msr psp, r0 \n" //устанавливаем указатель стека новой задачи
        "isb \n"

        "cpsie i \n"
        "bx lr \n"
    );
}

void Clock_Init(void) {
    //1. Включаем внешний кварцевый генератор на 25 МГц
    RCC->CR |= RCC_CR_HSEON;
    while(!(RCC->CR & RCC_CR_HSERDY));

    //2. Включаем тактирование модуля Power Control
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;

    //3. Настраиваем Flash-память: включаем кэш инструкций, кэш данных, Prefetch и ставим задержку 3 WS
    FLASH->ACR |= FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_3WS;

    // 4. Настраиваем делители для системных шин AHB, APB1, APB2
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;  // AHB = SYSCLK (100 МГц)
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1; // APB2 = AHB / 1  (100 МГц — максимум)
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2; // APB1 = AHB / 2  (50 МГц — ОГРАНИЧЕНИЕ ШИНЫ!)

    // 5. Конфигурируем блок PLL под кварц 25 МГц для получения 100 МГц
    // Сбрасываем старые биты M, N, P и источника
    RCC->PLLCFGR &= ~(RCC_PLLCFGR_PLLM | RCC_PLLCFGR_PLLN | RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLSRC);
    
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE; // Источник — внешний кварц
    RCC->PLLCFGR |= (25 << RCC_PLLCFGR_PLLM_Pos); // Делитель M = 25 (25MHz / 25 = 1MHz)
    RCC->PLLCFGR |= (400 << RCC_PLLCFGR_PLLN_Pos); // Умножитель N = 400 (1MHz * 400 = 400MHz)
    RCC->PLLCFGR |= (1 << RCC_PLLCFGR_PLLP_Pos);   // Делитель P = 4 (В битовой маске '1' означает деление на 4: 00=2, 01=4, 10=6...)

    // Для правильной работы USB (если потребуется) делим частоту VCO на 8, чтобы получить 50 МГц (близко к стандартным 48 МГц)
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLQ;
    RCC->PLLCFGR |= (8 << RCC_PLLCFGR_PLLQ_Pos);

    // 6. Включаем блок PLL в работу
    RCC->CR |= RCC_CR_PLLON;
    while((RCC->CR & RCC_CR_PLLRDY) == 0);

    // 7. Переключаем процессор на тактирование от разогнанного блока PLL
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    
    // Ожидаем, пока аппаратура подтвердит успешную смену источника частоты
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}
