#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "nyx/compiler.h"
#include "nyx/debug.h"
#include "nyx/memory.h"
#include "nyx/natives.h"
#include "nyx/vm.h"

VM vm;

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *function = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);
        fprintf(stderr, "  [line %d] in %s\n", function->chunk.lines[instruction],
                function->name == NULL ? "script" : function->name->chars);
    }

    resetStack();
}

void initVM(void) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);

    registerNatives();
}

void freeVM(void) {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(void) {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) { return vm.stackTop[-1 - distance]; }

void defineNative(const char *name, NativeFn function, int arity) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function, arity, name)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
    ObjString *b = AS_STRING(peek(0));
    ObjString *a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, (size_t)a->length);
    memcpy(chars + a->length, b->chars, (size_t)b->length);
    chars[length] = '\0';

    ObjString *result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

static bool call(ObjClosure *closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == NYX_MAX_CALL_FRAMES) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                ObjNative *native = AS_NATIVE(callee);
                if (native->arity != -1 && argCount != native->arity) {
                    runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
                    return false;
                }
                NativeResult result = native->function(argCount, vm.stackTop - argCount);
                if (result.isError) {
                    runtimeError("%s", AS_CSTRING(result.value));
                    return false;
                }
                vm.stackTop -= argCount + 1;
                push(result.value);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions.");
    return false;
}

/* Upvalues that point at the same stack slot must be the same ObjUpvalue
 * (otherwise two closures over the same local would drift out of sync the
 * moment one of them wrote to it) -- so open upvalues live in a linked
 * list, sorted by stack depth, and this walks it to find-or-create. */
static ObjUpvalue *captureUpvalue(Value *local) {
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value *last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static bool indexGet(Value container, Value indexVal, Value *out) {
    if (IS_ARRAY(container)) {
        if (!IS_NUMBER(indexVal)) {
            runtimeError("Array index must be a number.");
            return false;
        }
        ObjArray *array = AS_ARRAY(container);
        double d = AS_NUMBER(indexVal);
        int i = (int)d;
        if (i < 0 || i >= array->items.count) {
            runtimeError("Array index %d out of bounds (length %d).", i, array->items.count);
            return false;
        }
        *out = array->items.values[i];
        return true;
    }
    if (IS_MAP(container)) {
        if (!IS_STRING(indexVal)) {
            runtimeError("Map keys must be strings.");
            return false;
        }
        ObjMap *map = AS_MAP(container);
        Value value;
        if (!tableGet(&map->table, AS_STRING(indexVal), &value)) {
            *out = NIL_VAL;
        } else {
            *out = value;
        }
        return true;
    }
    if (IS_STRING(container)) {
        if (!IS_NUMBER(indexVal)) {
            runtimeError("String index must be a number.");
            return false;
        }
        ObjString *s = AS_STRING(container);
        int i = (int)AS_NUMBER(indexVal);
        if (i < 0 || i >= s->length) {
            runtimeError("String index %d out of bounds (length %d).", i, s->length);
            return false;
        }
        *out = OBJ_VAL(copyString(s->chars + i, 1));
        return true;
    }
    runtimeError("Only arrays, maps and strings support indexing.");
    return false;
}

static bool indexSet(Value container, Value indexVal, Value value) {
    if (IS_ARRAY(container)) {
        if (!IS_NUMBER(indexVal)) {
            runtimeError("Array index must be a number.");
            return false;
        }
        ObjArray *array = AS_ARRAY(container);
        int i = (int)AS_NUMBER(indexVal);
        if (i < 0 || i >= array->items.count) {
            runtimeError("Array index %d out of bounds (length %d).", i, array->items.count);
            return false;
        }
        array->items.values[i] = value;
        return true;
    }
    if (IS_MAP(container)) {
        if (!IS_STRING(indexVal)) {
            runtimeError("Map keys must be strings.");
            return false;
        }
        ObjMap *map = AS_MAP(container);
        tableSet(&map->table, AS_STRING(indexVal), value);
        return true;
    }
    runtimeError("Only arrays and maps support index assignment.");
    return false;
}

static InterpretResult run(void) {
    CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op)                                     \
    do {                                                             \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {            \
            runtimeError("Operands must be numbers.");                \
            return INTERPRET_RUNTIME_ERROR;                          \
        }                                                            \
        double b = AS_NUMBER(pop());                                 \
        double a = AS_NUMBER(pop());                                 \
        push(valueType(a op b));                                     \
    } while (false)

    for (;;) {
#ifdef NYX_TRACE_EXECUTION
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
                                (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_DUP: push(peek(0)); break;

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString *name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString *name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString *name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }

            case OP_GET_INDEX: {
                Value indexVal = pop();
                Value container = pop();
                Value result;
                if (!indexGet(container, indexVal, &result)) return INTERPRET_RUNTIME_ERROR;
                push(result);
                break;
            }
            case OP_SET_INDEX: {
                Value value = pop();
                Value indexVal = pop();
                Value container = pop();
                if (!indexSet(container, indexVal, value)) return INTERPRET_RUNTIME_ERROR;
                push(value);
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (AS_NUMBER(peek(0)) == 0) {
                    runtimeError("Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a / b));
                break;
            }
            case OP_MODULO: {
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                if (b == 0) {
                    runtimeError("Modulo by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(fmod(a, b)));
                break;
            }
            case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_NEGATE: {
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure *closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }
                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_ARRAY: {
                uint8_t count = READ_BYTE();
                ObjArray *array = newArray();
                push(OBJ_VAL(array)); /* GC root while we fill it */
                for (int i = count; i >= 1; i--) {
                    writeValueArray(&array->items, vm.stackTop[-1 - i]);
                }
                Value arrayVal = pop();
                vm.stackTop -= count;
                push(arrayVal);
                break;
            }
            case OP_MAP: {
                uint8_t pairCount = READ_BYTE();
                ObjMap *map = newMap();
                push(OBJ_VAL(map));
                for (int i = pairCount; i >= 1; i--) {
                    Value key = vm.stackTop[-1 - (2 * i)];
                    Value value = vm.stackTop[-1 - (2 * i - 1)];
                    if (!IS_STRING(key)) {
                        runtimeError("Map keys must be strings.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    tableSet(&map->table, AS_STRING(key), value);
                }
                Value mapVal = pop();
                vm.stackTop -= (size_t)pairCount * 2;
                push(mapVal);
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
    ObjFunction *function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}

InterpretResult interpretChunkForDisasm(const char *source, ObjFunction **outFn) {
    ObjFunction *function = compile(source);
    *outFn = function;
    return function == NULL ? INTERPRET_COMPILE_ERROR : INTERPRET_OK;
}
