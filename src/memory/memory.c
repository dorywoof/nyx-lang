#include <stdio.h>
#include <stdlib.h>

#include "nyx/common.h"
#include "nyx/memory.h"
#include "nyx/vm.h"

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;

    if (newSize > oldSize) {
#ifdef NYX_STRESS_GC
        collectGarbage();
#else
        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
#endif
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "nyx: out of memory (requested %zu bytes)\n", newSize);
        exit(74);
    }
    return result;
}
