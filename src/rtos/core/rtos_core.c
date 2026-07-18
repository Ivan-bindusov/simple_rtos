#include "rtos_core.h"
#include "core/rtos_log.h"
#include "../bsp/stm32f411/uart.h"

//выделение памяти под структуры ОС
TCB_t TCBs[MAX_TASKS];
uint32_t currentTask = 0;
uint32_t __attribute__((aligned(8))) TaskStacks[MAX_TASKS][STACK_SIZE];

void OS_StackOverflow_Handler(uint32_t brokenTaskIndex) {
    __asm volatile ("cpsid i");

    UART2_SendString("AHTUNG! Stack overflow. In the task: ");
    UART2_SendChar('0' + brokenTaskIndex);
    UART2_SendString("\n\r");

    OS_Log_Write((uint8_t)brokenTaskIndex, "AHTUNG: Stack Overflow was detected. Canary corrupted");

    //быстро мигаем светодиодом
    while(1) {
        GPIOC->ODR ^= GPIO_ODR_ODR_13;
        for(volatile uint32_t i=0;i<200000;i++);
    }
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