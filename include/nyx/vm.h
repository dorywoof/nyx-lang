#ifndef NYX_VM_H
#define NYX_VM_H

#include "nyx/chunk.h"
#include "nyx/common.h"
#include "nyx/object.h"
#include "nyx/table.h"
#include "nyx/value.h"

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

typedef struct {
    CallFrame frames[NYX_MAX_CALL_FRAMES];
    int frameCount;

    Value stack[NYX_MAX_STACK];
    Value *stackTop;

    Table globals;
    Table strings;

    ObjUpvalue *openUpvalues;

    Obj *objects;
    size_t bytesAllocated;
    size_t nextGC;
    int grayCount;
    int grayCapacity;
    Obj **grayStack;
} VM;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char *source);
InterpretResult interpretChunkForDisasm(const char *source, ObjFunction **outFn);

void push(Value value);
Value pop(void);
Value peek(int distance);

void defineNative(const char *name, NativeFn function, int arity);

void runtimeError(const char *format, ...);

#endif
