#ifndef RTOS_HEAP_H
#define RTOS_HEAP_H

#include <stdint.h> 
#include <stddef.h>
#include "stm32f411xe.h"

#define HEAP_SIZE 8192 // Куча TSLF на 8KB внутри RAM

// Параметры TSLF кучи
#define FL_INDEX_BITS 10 // 8 уровней (степени двойки от 16 байт до 4 КБ)
#define SL_INDEX_BITS 2 // 4 под-диапазона в каждом уровне
#define SL_COUNT (1 << SL_INDEX_BITS)

typedef struct BlockHeader_t {
    struct BlockHeader_t *prevPhysicalBlock; // ссылка на соседа слева
    uint32_t sizeAndFlags; // размер блока и флаг (занят/свободен)
    struct BlockHeader_t* nextFree;
    struct BlockHeader_t* prevFree;
} BlockHeader_t;

// Структура управления кучей
typedef struct {
    uint32_t flBitmap; // Битовая маска первого уровня
    uint32_t slBitmap[FL_INDEX_BITS]; // Битовые маски второго уровня
    BlockHeader_t* matrix[FL_INDEX_BITS][SL_COUNT]; // Таблица указателей на свободные блоки
} TLSF_Pool_t;

extern TLSF_Pool_t rtosHeap;

void OS_Heap_Init(void);
void* OS_Malloc(uint32_t size);
void OS_Free(void* ptr);

#endif