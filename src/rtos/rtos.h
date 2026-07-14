#ifndef RTOS_H_
#define RTOS_H_

#include <stdint.h>
#include "stm32f411xe.h"
#include <stddef.h>

#define MAX_TASKS   5
#define STACK_SIZE  256
#define QUEUE_SIZE  8

#define MAX_TIMERS 4

#define SYS_TICK_LOAD 100000000U

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

typedef void (*TimerCallback_t)(void);

typedef struct {
    void *sp;               // Текущий указатель стека задачи
    uint32_t ticksToWait;   // Счетчик ожидания
    uint8_t priority;       // Текущий рабочий приоритет задачи
    uint8_t basePriority;   // Базовый (начальный) уровень приориета
    uint8_t is_active;      // Флаг валидности задачи, чтобы не выполнять не инициализированную задачу
} __attribute__((aligned(16))) TCB_t;

typedef struct {
    volatile uint8_t locked;
    volatile uint8_t ownerTask;
    volatile uint32_t blockedTasks;  // Битовая маска заблокированных по мьютексам задач
} Mutex_t;

typedef struct {
    uint32_t buffer[QUEUE_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint8_t count;
} Queue_t;

typedef struct {
    uint32_t period;
    uint32_t ticksLeft;
    uint8_t autoReload;
    uint8_t active;
    TimerCallback_t callback;
} OS_Timer_t;

typedef struct {
    uint32_t count;  // Текущее значение счетчика разрешений
    uint32_t maxCount;  // Максимальный предел (1 для бинарного, >1 для счетного)
    uint8_t blockedTasks;   // Битовая маска заблокированных задач
} Semaphore_t;

//глобальные переменные ядра
extern TCB_t TCBs[MAX_TASKS];    // Массив всех задач
extern uint32_t currentTask;  // Индекс активной задачи
extern uint32_t TaskStacks[MAX_TASKS][STACK_SIZE];

void OS_Heap_Init(void);
void* OS_Malloc(uint32_t size);
void OS_Free(void* ptr);

//системные функции ядра
void Clock_Init(void);
void SysTick_Init(uint32_t load);
void Task_Create(uint8_t taskIndex, uint8_t priority, void (*taskFunc)(void));
void OS_Start(void (*firstTaskFunc)(void));
uint32_t* os_scheduler(void);
void OS_Delay(uint32_t ms);
void OS_StackOverflow_Handler(uint32_t brokenTaskIndex);

//функции для работы с мьютексами
void Mutex_Init(Mutex_t *mutex);
uint8_t Mutex_Lock(Mutex_t *mutex, uint32_t timeout);
void Mutex_Unlock(Mutex_t *mutex);

//функции для работы с очередями
void Queue_Init(Queue_t *q);
uint8_t Queue_Send(Queue_t *q, uint32_t data);
uint8_t Queue_Receive(Queue_t *q, uint32_t *data);

void OS_Timer_Create(uint8_t id, uint32_t period, uint8_t autoReload, TimerCallback_t callback);
void OS_Timer_Start(uint8_t id);
void OS_Timer_Stop(uint8_t id);
void OS_Timers_Process(void);

void Semaphore_Init(Semaphore_t *sem, uint32_t initialCount, uint32_t maxCount);
uint8_t Semaphore_Take(Semaphore_t *sem, uint32_t timeout); // Взять разрешение
void Semaphore_Give(Semaphore_t *sem); // Вернуть разрешение

#endif