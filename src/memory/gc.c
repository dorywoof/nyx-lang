#include <stdio.h>
#include <stdlib.h>

#include "nyx/compiler.h"
#include "nyx/memory.h"
#include "nyx/object.h"
#include "nyx/vm.h"

#define GC_HEAP_GROW_FACTOR 2

/*
 * Mark-sweep, tri-color, non-generational, non-incremental. It stops the
 * world (there's only one thread) whenever bytesAllocated crosses nextGC.
 *
 * Why mark-sweep and not reference counting? Refcounting is simpler to
 * reason about locally (every store/overwrite touches a counter) but it
 * cannot free cycles on its own -- and Nyx has cycles the moment a closure
 * captures a variable that (directly or through another closure) ends up
 * holding a reference back to itself, or a map/array holds itself. A
 * teaching VM that "mostly" collects memory is worse than one that always
 * does, so this trades a periodic pause for correctness with zero extra
 * bookkeeping per assignment. See docs/study-guide.md for the interview
 * version of this answer.
 */

static void markRoots(void) {
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj *)vm.frames[i].closure);
    }

    for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj *)upvalue);
    }

    markTable(&vm.globals);
    markCompilerRoots();
}

static void markArray(ValueArray *array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

/* "Blacken" = an object has already been marked gray (reachable, not yet
 * scanned); this walks its own references and marks each of those gray too,
 * turning the object itself black (reachable, fully scanned). */
static void blackenObject(Obj *object) {
#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: blacken %p (type %d)\n", (void *)object, object->type);
#endif

    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE:
            break; /* no outgoing references */

        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *)object;
            if (function->name != NULL) markObject((Obj *)function->name);
            markArray(&function->chunk.constants);
            break;
        }

        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *)object;
            markObject((Obj *)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj *)closure->upvalues[i]);
            }
            break;
        }

        case OBJ_UPVALUE:
            markValue(((ObjUpvalue *)object)->closed);
            break;

        case OBJ_ARRAY:
            markArray(&((ObjArray *)object)->items);
            break;

        case OBJ_MAP:
            markTable(&((ObjMap *)object)->table);
            break;
    }
}

static void freeObject(Obj *object) {
#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: free %p (type %d)\n", (void *)object, object->type);
#endif

    switch (object->type) {
        case OBJ_STRING: {
            ObjString *string = (ObjString *)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *)object;
            FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
        case OBJ_ARRAY: {
            ObjArray *array = (ObjArray *)object;
            freeValueArray(&array->items);
            FREE(ObjArray, object);
            break;
        }
        case OBJ_MAP: {
            ObjMap *map = (ObjMap *)object;
            freeTable(&map->table);
            FREE(ObjMap, object);
            break;
        }
    }
}

static void traceReferences(void) {
    while (vm.grayCount > 0) {
        Obj *object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep(void) {
    Obj *previous = NULL;
    Obj *object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false; /* reset for next cycle */
            previous = object;
            object = object->next;
        } else {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

void markObject(Obj *object) {
    if (object == NULL) return;
    if (object->isMarked) return;

#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: mark %p (type %d)\n", (void *)object, object->type);
#endif

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        /* Deliberately NOT going through reallocate(): the gray stack is
         * scratch space for the collector itself, so growing it must not
         * recursively trigger another collection mid-collection. */
        vm.grayStack = (Obj **)realloc(vm.grayStack, sizeof(Obj *) * (size_t)vm.grayCapacity);
        if (vm.grayStack == NULL) {
            fprintf(stderr, "nyx: out of memory growing the GC gray stack\n");
            exit(74);
        }
    }
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void collectGarbage(void) {
#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: begin (%zu bytes allocated)\n", vm.bytesAllocated);
#endif

    markRoots();
    traceReferences();
    /* The intern table holds every live string, but it must not itself
     * keep strings alive -- otherwise no string would ever be collected.
     * So we sweep it of anything not marked by real references first. */
    tableRemoveWhiteKeys(&vm.strings);
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: end (%zu bytes allocated, next at %zu)\n", vm.bytesAllocated, vm.nextGC);
#endif
}

void freeObjects(void) {
    Obj *object = vm.objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
}
