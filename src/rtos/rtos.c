#include "rtos.h"

#define STACK_CANARY_WORD 0xDEADBEEF;

//выделение памяти под структуры ОС
TCB_t TCBs[MAX_TASKS];
uint32_t currentTask = 0;
uint32_t __attribute__((aligned(8))) TaskStacks[MAX_TASKS][STACK_SIZE];

TLSF_Pool_t rtosHeap;
uint8_t heapMemory[HEAP_SIZE] __attribute__((aligned(4))); // физическая память кучи

OS_Timer_t OSTimers[MAX_TIMERS];

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

void Task_Create(uint8_t taskIndex, uint8_t priority, void (*taskFunc)(void)) {
    
    TaskStacks[taskIndex][0] = STACK_CANARY_WORD;
    
    uint32_t *sp = &TaskStacks[taskIndex][STACK_SIZE];

    // Имитация аппаратного контекста (Сохраняется процессором автоматически)
    *(--sp) = 0x01000000;         // xPSR (Бит Thumb режима)
    *(--sp) = (uint32_t)taskFunc; // PC (Адрес функции задачи)
    *(--sp) = 0xFFFFFFFD;         // LR (Возврат в Thread Mode с PSP)
    *(--sp) = 0x12121212;         // R12
    *(--sp) = 0x03030303;         // R3
    *(--sp) = 0x02020202;         // R2
    *(--sp) = 0x01010101;         // R1
    *(--sp) = 0x00000000;         // R0

    // Имитация программного контекста (Сохраняется нами вручную)
    *(--sp) = 0x00000000;         // R11
    *(--sp) = 0x00000000;         // R10
    *(--sp) = 0x00000000;         // R9
    *(--sp) = 0x00000000;         // R8
    *(--sp) = 0x00000000;         // R7
    *(--sp) = 0x00000000;         // R6
    *(--sp) = 0x00000000;         // R5
    *(--sp) = 0x00000000;         // R4

    TCBs[taskIndex].sp = sp;
    TCBs[taskIndex].priority = priority;
    TCBs[taskIndex].basePriority = priority;
    TCBs[taskIndex].ticksToWait = 0;
    TCBs[taskIndex].is_active = 1;
}

uint32_t* os_scheduler(void) {

    if (TaskStacks[currentTask][0] != 0xDEADBEEF) {
        OS_StackOverflow_Handler(currentTask);
    }

    int8_t highestPriorityTask = -1;
    uint8_t highestPriorityValue = 255; //задаем худший приоритет (Idle)

    // Сканируем весь массив задач в системе 
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        // Если задача готова не спит
        if (TCBs[i].is_active == 1 && TCBs[i].ticksToWait == 0) {
            // И ее приоритет выше чем текущий (число меньше)
            if (TCBs[i].priority < highestPriorityValue) {
                highestPriorityValue = TCBs[i].priority;
                highestPriorityTask = i; // Запоминаем индекс самой важной задачи
            }
        }
    }

    if (highestPriorityTask != -1) {
        currentTask = highestPriorityTask;
        return (uint32_t*)TCBs[currentTask].sp;
    }

    currentTask = MAX_TASKS - 1;
    return (uint32_t*)TCBs[MAX_TASKS - 1].sp;
}

void OS_Delay(uint32_t ms) {
    __asm volatile ("cpsid i");
    TCBs[currentTask].ticksToWait = ms;
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    __asm volatile ("cpsie i");
    __asm volatile ("isb");
}

void OS_Start(void (*firstTaskFunc)(void)) {
    __asm volatile (
        "cpsid i \n"
        "ldr r0, =TCBs \n"            
        "ldr r0, [r0] \n"             // Берем SP первой задачи
        "ldmia r0!, {r4-r11} \n"      // Восстанавливаем программные регистры
        "msr psp, r0 \n"              /* 1. Записали адрес стека Task1 в скрытый регистр PSP */
        "mov r0, #2 \n"               // Переключаем ядро на работу со стеком процессов (PSP)
        "msr control, r0 \n"
        "isb \n"
        "cpsie i \n"

        "blx %[task_addr] \n"             // Прыгаем в первую задачу
        :
        : [task_addr] "r" (firstTaskFunc) // Передаем адрес функции первой задачи в ассемблерный код
        : "r0" //список регистров, которые будут очищены
    );
}

void OS_StackOverflow_Handler(uint32_t brokenTaskIndex) {
    __asm volatile ("cpsid i");

    //быстро мигаем светодиодом
    while(1) {
        GPIOC->ODR ^= GPIO_ODR_ODR_13;
        for(volatile uint32_t i=0;i<200000;i++);
    }
}

// Вычисление строки (FL) и столбца (SL) по размеру блока
static void tlsf_mapping(uint32_t size, int* fl, int* sl) {
    if (size < 16) size = 16; // Минимальный размер блока

    // Подсчет ведущих нулей
    *fl = 31 - __builtin_clz(size);
    *sl = (size >> (*fl - SL_INDEX_BITS)) ^ SL_COUNT;
    *fl -= 4; // Сдвиг под минимальный размер (16 байт = 2 ^ 4)
}

void OS_Heap_Init(void) {
    // Обнуляем bitmaps и матрицу указателей
    rtosHeap.flBitmap = 0;
    for (int i=0;i<FL_INDEX_BITS;i++) {
        rtosHeap.slBitmap[i] = 0;
        for (int j=0;j<SL_COUNT;j++) {
            rtosHeap.matrix[i][j] = NULL;
        }
    }

    // Создаем один гигантский блок из всего массива heapMemory
    BlockHeader_t* initialBlock = (BlockHeader_t*)heapMemory;
    initialBlock->prevPhysicalBlock = NULL;
    initialBlock->sizeAndFlags = (HEAP_SIZE - sizeof(BlockHeader_t)) | 0x01; // 0x01 - флаг свободен

    // Вычисляем индексы для этого стартового гигантского блока
    int fl, sl;
    tlsf_mapping(HEAP_SIZE, &fl, &sl);

    // Кладем его в матрицу и взводим биты в масках
    rtosHeap.matrix[fl][sl] = initialBlock;
    rtosHeap.flBitmap |= (1 << fl);
    rtosHeap.slBitmap[fl] |= (1 << sl);

    initialBlock->nextFree = NULL;
    initialBlock->prevFree = NULL;
}

void* OS_Malloc(uint32_t size) {
    __asm volatile ("cpsid i");

    // Выравниваем размер под 4 байта (требование шины ARM)
    size = (size + 3) & ~3;

    int fl, sl;
    tlsf_mapping(size, &fl, &sl);

    // Ищем подходящий блок в матрице
    BlockHeader_t* block = rtosHeap.matrix[fl][sl];

    if (block == NULL) {

    }

    // Если блок найден - помечаем его как занятый (сбрасываем флаг 0x01)
    block->sizeAndFlags &= ~0x01;

    // Убираем его из списка свободных
    rtosHeap.matrix[fl][sl] = block->nextFree;
    if (block->nextFree) block->nextFree->prevFree = NULL;

    // Если в ячейке больше нет блоков, гасим биты в маске
    if (rtosHeap.matrix[fl][sl] == NULL) {
        rtosHeap.slBitmap[fl] &= ~(1 << sl);
        if (rtosHeap.slBitmap[fl] == 0) {
            rtosHeap.flBitmap &= ~(1 << fl);
        }
    }

    __asm volatile ("cpsie i");

    // Возвращаем указатель на память сразу после заголовка блока
    return (void*)((uint8_t*)block + sizeof(BlockHeader_t));
}

void OS_Timer_Create(uint8_t id, uint32_t period, uint8_t autoReload, TimerCallback_t callback) {
    if (id >= MAX_TIMERS) return;

    __asm volatile ("cpsid i");
    OSTimers[id].period = period;
    OSTimers[id].ticksLeft = period;
    OSTimers[id].autoReload = autoReload;
    OSTimers[id].callback = callback;
    OSTimers[id].active = 0;
    __asm volatile ("cpsie i");
}

void OS_Timer_Start(uint8_t id) {
    if (id >= MAX_TIMERS) return;
    __asm volatile ("cpsid i");
    OSTimers[id].ticksLeft = OSTimers[id].period;
    OSTimers[id].active = 1;
    __asm volatile ("cpsie i");
}

void OS_Timer_Stop(uint8_t id) {
    if(id >= MAX_TIMERS) return;
    OSTimers[id].active = 0;
}

void OS_Timers_Process(void) {
    for (uint8_t i = 0; i < MAX_TIMERS; i++) {
        if (OSTimers[i].active && OSTimers[i].callback) {
            if (OSTimers[i].ticksLeft > 0) {
                OSTimers[i].ticksLeft--;
            }

            if (OSTimers[i].ticksLeft == 0) {
                OSTimers[i].callback();

                if (OSTimers[i].autoReload) {
                    OSTimers[i].ticksLeft = OSTimers[i].period;
                } else {
                    OSTimers[i].active = 0;
                }
            }
        }
    }
}

void Semaphore_Init(Semaphore_t *sem, uint32_t initialCount, uint32_t maxCount) {
    sem->count = initialCount;
    sem->maxCount = maxCount;
    sem->blockedTasks = 0;
}

uint8_t Semaphore_Take(Semaphore_t *sem, uint32_t timeout) {
    __asm volatile ("cpsid i");

    if (sem->count > 0) {
        sem->count--; // Забираем одно разрешение
        __asm volatile ("cpsie i");
        return 1;
    }

    // Если разрешений нет, а таймаут равен нулю - уходим ни с чем
    if (timeout == 0) {
        __asm volatile("cpsie i");
        return 0;
    }

    // Блокируем текущую задачу
    sem->blockedTasks |= (1 << currentTask);
    TCBs[currentTask].ticksToWait = timeout;

    __asm volatile ("cpsie i");

    // Актвируем планировщик, чтобы переключить процессор на другую задачу
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    __ISB();

    // (sem->blockedTasks & (1 << currentTask)) == 0 - значит что задачу "разбудили"
    __asm volatile ("cpsid i");
    if ((sem->blockedTasks & (1 << currentTask)) == 0) {
        __asm volatile("cpsie i");
        return 1; // Задача разблокирована
    }

    // Если проснулись по таймауту, просто забираем себя из списка ожидания
    sem->blockedTasks &= ~(1 << currentTask);
    __asm volatile ("cpsie i");
    return 0; // Сбой по таймауту
}

// Освобождение семафора (выстрел сигнальной ракеты)
void Semaphore_Give(Semaphore_t *sem) {
    __asm volatile("cpsid i");

    // Если ктото ожидает этот семафор
    if (sem->blockedTasks != 0) {
        int8_t highPriorityTaskIndex = -1;
        uint8_t highPriorityTaskValue = 255;

        // Ищем задачу наивысшего приоритета, среди тех, которые стоят в ожидании семафора
        for (uint8_t i=0;i<MAX_TASKS;i++) {
            if (sem->blockedTasks & (1 << i)) {
                if (TCBs[i].priority < highPriorityTaskValue) {
                    highPriorityTaskValue = TCBs[i].priority;
                    highPriorityTaskIndex = i;
                }
            }
        }

        if (highPriorityTaskIndex != -1) {
            // Пробуждаем задачу
            sem->blockedTasks &= ~(1 << highPriorityTaskIndex);
            TCBs[highPriorityTaskIndex].ticksToWait = 0;

            __asm volatile ("cpsie i");

            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
            return;
        }
    }

    // Если ни кто не ждал, увеличиваем счетчик - стандартный "накопленный" режим семафора
    if (sem->count < sem->maxCount) {
        sem->count++;
    }

    __asm volatile ("cpsie i");
}