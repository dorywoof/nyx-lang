#ifndef NYX_VM_H
#define NYX_VM_H

#include "nyx/chunk.h"
#include "nyx/common.h"
#include "nyx/object.h"
#include "nyx/table.h"
#include "nyx/value.h"

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;   /* next instruction to execute, inside closure->function->chunk */
    Value *slots;  /* this frame's window into vm.stack (slot 0 = the closure itself) */
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
    Table strings; /* interned string pool, weak-referenced by the GC */

    ObjUpvalue *openUpvalues;

    /* GC bookkeeping */
    Obj *objects; /* every heap object, for sweep() */
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

/* Registers a C function as a Nyx global callable value. arity of -1 means
 * variadic (the native itself validates argCount). Used by src/stdlib. */
void defineNative(const char *name, NativeFn function, int arity);

/* Raises a runtime error mid-execution; only meaningful while `run()` is on
 * the call stack (i.e. from opcode handlers or from a native function). */
void runtimeError(const char *format, ...);

#endif
