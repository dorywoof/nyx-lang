#ifndef NYX_OBJECT_H
#define NYX_OBJECT_H

#include "nyx/chunk.h"
#include "nyx/common.h"
#include "nyx/table.h"
#include "nyx/value.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_MAP,
} ObjType;

/* Every heap object starts with this header so the GC can walk the
 * VM-owned linked list of all objects (vm.objects) without knowing the
 * concrete type -- see gc.c sweep(). */
struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj *next;
};

struct ObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash; /* cached: computed once at creation, used on every table op */
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString *name; /* NULL for the implicit top-level script function */
} ObjFunction;

typedef struct {
    bool isError;
    Value value; /* result value, or an error-message string when isError */
} NativeResult;

typedef NativeResult (*NativeFn)(int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
    int arity; /* -1 means variadic */
    ObjString *name;
} ObjNative;

typedef struct ObjUpvalue {
    Obj obj;
    Value *location;      /* while open: points into a live VM stack slot */
    Value closed;          /* while closed: holds the value itself */
    struct ObjUpvalue *next; /* VM's sorted open-upvalue list */
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ValueArray items;
} ObjArray;

typedef struct {
    Obj obj;
    Table table; /* Nyx maps are string-keyed only -- see docs/architecture.md */
} ObjMap;

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)
#define IS_MAP(value) isObjType(value, OBJ_MAP)

#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_ARRAY(value) ((ObjArray *)AS_OBJ(value))
#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjString *copyString(const char *chars, int length);
ObjString *takeString(char *chars, int length);
ObjFunction *newFunction(void);
ObjNative *newNative(NativeFn function, int arity, const char *name);
ObjClosure *newClosure(ObjFunction *function);
ObjUpvalue *newUpvalue(Value *slot);
ObjArray *newArray(void);
ObjMap *newMap(void);

const char *objTypeName(ObjType type);
void printObject(Value value);

#endif
