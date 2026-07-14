#include <stdio.h>
#include <string.h>

#include "nyx/memory.h"
#include "nyx/object.h"
#include "nyx/vm.h"

#define ALLOCATE_OBJ(type, objectType) (type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type) {
    Obj *object = (Obj *)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#ifdef NYX_LOG_GC
    fprintf(stderr, "gc: alloc %p (%zu bytes, type %d)\n", (void *)object, size, type);
#endif

    return object;
}

static uint32_t hashString(const char *key, int length) {
    /* FNV-1a. Not cryptographic, not meant to be -- just fast and
     * well-distributed enough for an identifier/string table. */
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619u;
    }
    return hash;
}

static ObjString *allocateString(char *chars, int length, uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    push(OBJ_VAL(string)); /* keep it alive while tableSet may allocate/GC */
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

ObjString *takeString(char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

ObjString *copyString(const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, (size_t)length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

ObjFunction *newFunction(void) {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative *newNative(NativeFn function, int arity, const char *name) {
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->arity = arity;
    push(OBJ_VAL(native));
    native->name = copyString(name, (int)strlen(name));
    pop();
    return native;
}

ObjClosure *newClosure(ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue *newUpvalue(Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjArray *newArray(void) {
    ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    initValueArray(&array->items);
    return array;
}

ObjMap *newMap(void) {
    ObjMap *map = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    initTable(&map->table);
    return map;
}

const char *objTypeName(ObjType type) {
    switch (type) {
        case OBJ_STRING: return "string";
        case OBJ_FUNCTION: return "function";
        case OBJ_NATIVE: return "native";
        case OBJ_CLOSURE: return "function";
        case OBJ_UPVALUE: return "upvalue";
        case OBJ_ARRAY: return "array";
        case OBJ_MAP: return "map";
    }
    return "unknown";
}

static void printFunction(ObjFunction *function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn %s>", AS_NATIVE(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_ARRAY: {
            ObjArray *array = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < array->items.count; i++) {
                if (i > 0) printf(", ");
                printValue(array->items.values[i]);
            }
            printf("]");
            break;
        }
        case OBJ_MAP: {
            ObjMap *map = AS_MAP(value);
            printf("{");
            bool first = true;
            for (int i = 0; i < map->table.capacity; i++) {
                Entry *entry = &map->table.entries[i];
                if (entry->key == NULL) continue;
                if (!first) printf(", ");
                first = false;
                printf("\"%s\": ", entry->key->chars);
                printValue(entry->value);
            }
            printf("}");
            break;
        }
    }
}
