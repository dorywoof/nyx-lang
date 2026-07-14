#include <stdio.h>
#include <string.h>

#include "nyx/memory.h"
#include "nyx/object.h"
#include "nyx/value.h"

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL:
            return true;
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NUMBER:
            return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:
            return AS_OBJ(a) == AS_OBJ(b);
    }
    return false;
}

void printValue(Value value) {
    switch (value.type) {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NUMBER:
            printf("%.14g", AS_NUMBER(value));
            break;
        case VAL_OBJ:
            printObject(value);
            break;
    }
}

void formatValue(char *buf, size_t size, Value value) {
    switch (value.type) {
        case VAL_NIL:
            snprintf(buf, size, "nil");
            break;
        case VAL_BOOL:
            snprintf(buf, size, "%s", AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NUMBER:
            snprintf(buf, size, "%.14g", AS_NUMBER(value));
            break;
        case VAL_OBJ:
            if (IS_STRING(value)) {
                snprintf(buf, size, "%s", AS_CSTRING(value));
            } else {
                snprintf(buf, size, "<%s>", objTypeName(OBJ_TYPE(value)));
            }
            break;
    }
}
