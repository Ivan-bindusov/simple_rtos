#include "rtos_heap.h"

TLSF_Pool_t rtosHeap;
uint8_t heapMemory[HEAP_SIZE] __attribute__((aligned(4))); // физическая память кучи

// Вычисление строки (FL) и столбца (SL) по размеру блока
static void tlsf_mapping(uint32_t size, int* fl, int* sl) {
    if (size < 16) size = 16; // Минимальный размер блока

    // Подсчет ведущих нулей
    *fl = 31 - __builtin_clz(size);
    *sl = (size >> (*fl - SL_INDEX_BITS)) ^ SL_COUNT;
    *fl -= 4; // Сдвиг под минимальный размер (16 байт = 2 ^ 4)
}

static void tlsf_remove_free_block(BlockHeader_t* block) {
    int fl, sl;
    uint32_t size = block->sizeAndFlags & ~0x01;
    tlsf_mapping(size, &fl, &sl);

    // Корректируем ссылки в двусвязном списке
    if (block->nextFree) block->nextFree->prevFree = block->prevFree;
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        // Если этот блок был первым в списке, перенаправляем матрицу указателей на следующий блок
        rtosHeap.matrix[fl][sl] = block->nextFree;
    }

    // Если ячейка памяти стала абсолютно пустой - гасим ее биты
    if (rtosHeap.matrix[fl][sl] == NULL) {
        rtosHeap.slBitmap[fl] &= ~(1 << sl);
        if (rtosHeap.slBitmap[fl] == 0) {
            // Если после очистки бита в slBitmap в нем больше не осталось битов, то гасим всю строку fl
            rtosHeap.flBitmap &= ~(1 << fl);
        }
    }
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
        // Проверяем есть ли свободные столбцы побольше в текущей строке
        uint32_t slMap = rtosHeap.slBitmap[fl] & (~0UL << (sl + 1));

        if (slMap != 0) {
            // Найден больший столбец в этой же строке
            sl = __builtin_ctz(slMap);
            block = rtosHeap.matrix[fl][sl];
        } else {
            // В текущей строке блоков, нужного размера больше нет
            uint32_t flMap = rtosHeap.flBitmap & (~0UL << (fl + 1));

            if (flMap == 0) {
                // AHTUNG! В куче физически нет ни одного свободного блока подходящего размера
                __asm volatile ("cpsie i");
                return NULL; // Ошибка, "Недостаточно памяти"
            }

            // Находим ближайшую старшую строку
            fl = __builtin_ctz(flMap);
            // В этой строке берем самый первый свободный столбец
            sl = __builtin_ctz(rtosHeap.slBitmap[fl]);

            block = rtosHeap.matrix[fl][sl];
        }
    }

    // Убираем его из списка свободных
    // захватываем первый попавшийся (не применяя поиск по двусвязному списку)
    rtosHeap.matrix[fl][sl] = block->nextFree;
    if (block->nextFree) block->nextFree->prevFree = NULL;

    // Если в ячейке больше нет блоков, гасим биты в маске
    if (rtosHeap.matrix[fl][sl] == NULL) {
        rtosHeap.slBitmap[fl] &= ~(1 << sl);
        if (rtosHeap.slBitmap[fl] == 0) {
            rtosHeap.flBitmap &= ~(1 << fl);
        }
    }

    uint32_t currentBlockSize = block->sizeAndFlags & ~0x01; // Чистый размер блока

    // Проверяем достаточно ли блок большой (16 байт для минимально возможного блока)
    if (currentBlockSize >= (size + sizeof(BlockHeader_t) + 16)) {
        // Находим адрес, где будет начинаться новый свободный блок
        BlockHeader_t* remainingBlock = (BlockHeader_t*)((uint8_t*)block + sizeof(BlockHeader_t) + size);

        // Вычисляем размер нового остатка
        uint32_t remainingSize = currentBlockSize - size - sizeof(BlockHeader_t);

        // Пишем заголовок нового, пустого оставшегося блока и взводим флаг - "свободен"
        remainingBlock->sizeAndFlags = remainingSize | 0x01;

        // Связываем остаточный блок по физической памяти с левым соседом
        remainingBlock->prevPhysicalBlock = block;

        // Текущий блок теперь уменшаем до размера, который просил пользователь
        block->sizeAndFlags = size;

        // Регистрируем новый свободный остаток в списке
        int remFl, remSl;
        tlsf_mapping(remainingSize, &remFl, &remSl);

        // Помещаем остаток в двузсвязный список свободных блоков ячейки (в начало очереди)
        remainingBlock->nextFree = rtosHeap.matrix[remFl][remSl];
        remainingBlock->prevFree = NULL;
        if (rtosHeap.matrix[remFl][remSl]) {
            rtosHeap.matrix[remFl][remSl]->prevFree = remainingBlock;
        }
        rtosHeap.matrix[remFl][remSl] = remainingBlock;

        // Взводим биты в масках для нового остатка
        rtosHeap.flBitmap |= (1 << remFl);
        rtosHeap.slBitmap[remFl] |= (1 << remSl);
    } else {
        // Если остаток слишком мал, дробить его не получится, отдаем его целиком
        block->sizeAndFlags &= ~0x01;
    }

    __asm volatile ("cpsie i");

    // Возвращаем указатель на память сразу после заголовка блока
    return (void*)((uint8_t*)block + sizeof(BlockHeader_t));
}

void OS_Free(void* ptr) {
    if (ptr == NULL) return;

    __asm volatile ("cpsid i");

    // Восстанавливаем пасспорт блока из указателя, переданным пользователем
    BlockHeader_t* block = (BlockHeader_t*)((uint8_t*)ptr - sizeof(BlockHeader_t));

    block->sizeAndFlags |= 0x01;
    uint32_t currentSize = block->sizeAndFlags & ~0x01;

    // Слияние с правым соседом
    BlockHeader_t* nextPhysical = (BlockHeader_t*)((uint8_t*)block + sizeof(BlockHeader_t) + currentSize);

    // Защита: правый сосед должен быть внутри физических границ массива кучи
    if ((uint8_t*)nextPhysical < (heapMemory + HEAP_SIZE)) {
        if (nextPhysical->sizeAndFlags & 0x01) { // Если правый сосед свободен
            // Вычеркиваем его из списка
            tlsf_remove_free_block(nextPhysical);

            // Поглощаем его размер и его паспорт
            currentSize += (nextPhysical->sizeAndFlags & ~0x01) + sizeof(BlockHeader_t);
            block->sizeAndFlags = currentSize | 0x01;

            // Если за правым соседом был еще кто-то, связываем его с ними
            BlockHeader_t* nextNextPhysical = (BlockHeader_t*)((uint8_t*)nextPhysical + sizeof(BlockHeader_t) +
                (nextPhysical->sizeAndFlags & ~0x01));
            if ((uint8_t*)nextNextPhysical < (heapMemory + HEAP_SIZE)) {
                nextNextPhysical->prevPhysicalBlock = block; // Блок после следующего не сливается, а просто ссылается на этот
            }
        }
    }

    // Слияние с левым соседом
    BlockHeader_t* prevPhysical = block->prevPhysicalBlock;
    if (prevPhysical != NULL && (prevPhysical->sizeAndFlags & 0x01)) {
        // Вычеркиваем левого соседа из списка т.к. его размер изменится
        tlsf_remove_free_block(prevPhysical);

        // Левый сосед поглощает нас
        uint32_t prevSize = prevPhysical->sizeAndFlags & ~0x01;
        prevSize += currentSize + sizeof(BlockHeader_t);
        prevPhysical->sizeAndFlags = prevSize | 0x01;

        // Перенаправляем главный указатель на левого соседа
        block = prevPhysical;
        currentSize = prevSize;

        // Следующий физический сосед связывается с нашим объединенным блоком
        BlockHeader_t* nextPhysicalAfterMerge = (BlockHeader_t*)((uint8_t*)block + sizeof(BlockHeader_t) + currentSize);
        if ((uint8_t*)nextPhysicalAfterMerge < (heapMemory + HEAP_SIZE)) {
            nextPhysicalAfterMerge->prevPhysicalBlock = block;
        }
    }

    // Регистрация финального объединенного блока в структуре TLSF
    int fl, sl;
    tlsf_mapping(currentSize, &fl, &sl);

    // Вставляем блок в начало двусвязного списка соответствующей ячейки
    block->nextFree = rtosHeap.matrix[fl][sl];
    block->prevFree = NULL;
    if (rtosHeap.matrix[fl][sl]) {
        rtosHeap.matrix[fl][sl]->prevFree = block;
    }
    rtosHeap.matrix[fl][sl] = block;

    // Устанавливаем биты, указвающие на наличие свободного блока соответствующего размера
    rtosHeap.flBitmap |= (1 << fl);
    rtosHeap.slBitmap[fl] |= (1 << sl);

    __asm volatile ("cpsie i");
}