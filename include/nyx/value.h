#ifndef NYX_VALUE_H
#define NYX_VALUE_H

#include "nyx/common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj *obj;
    } as;
} Value;

#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define NIL_VAL ((Value){VAL_NIL, {.number = 0}})
#define BOOL_VAL(b) ((Value){VAL_BOOL, {.boolean = (b)}})
#define NUMBER_VAL(n) ((Value){VAL_NUMBER, {.number = (n)}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)(object)}})

typedef struct {
    int count;
    int capacity;
    Value *values;
} ValueArray;

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);

bool valuesEqual(Value a, Value b);
void printValue(Value value);
void formatValue(char *buf, size_t size, Value value);

#endif
