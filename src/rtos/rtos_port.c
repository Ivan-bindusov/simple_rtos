#include "rtos.h"

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