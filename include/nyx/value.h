#ifndef NYX_VALUE_H
#define NYX_VALUE_H

#include "nyx/common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

/*
 * Nyx values are represented as a tagged union rather than NaN-boxing.
 * NaN-boxing packs every value into a single 64-bit double by hiding type
 * tags inside the unused bit patterns of NaN doubles -- it's faster (no
 * branch on type, half the memory) but unreadable without a diagram and a
 * lot of bit-twiddling commentary. A tagged union is what every mainstream
 * teaching VM (and plenty of production ones) uses because the size cost is
 * paid once per stack slot, not once per operation, and the code stays
 * something a reviewer can read top to bottom. See docs/architecture.md.
 */
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
/* Writes a printable representation into buf (truncated to size). Used by
 * the str() native and by error messages that can't just fprintf. */
void formatValue(char *buf, size_t size, Value value);

#endif
