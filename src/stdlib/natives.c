#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nyx/memory.h"
#include "nyx/natives.h"
#include "nyx/object.h"
#include "nyx/vm.h"

/* Every native returns a NativeResult instead of throwing through a global
 * VM pointer -- it keeps the C<->Nyx boundary explicit (see
 * docs/study-guide.md "why NativeResult and not a VM-wide error flag"). */
static NativeResult ok(Value value) { return (NativeResult){false, value}; }

static NativeResult nativeErrorf(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return (NativeResult){true, OBJ_VAL(copyString(buf, (int)strlen(buf)))};
}

static const char *valueTypeName(Value v) {
    if (IS_NIL(v)) return "nil";
    if (IS_BOOL(v)) return "bool";
    if (IS_NUMBER(v)) return "number";
    if (IS_OBJ(v)) {
        ObjType t = OBJ_TYPE(v);
        if (t == OBJ_CLOSURE || t == OBJ_FUNCTION || t == OBJ_NATIVE) return "function";
        return objTypeName(t);
    }
    return "unknown";
}

static NativeResult nativeClock(int argCount, Value *args) {
    (void)argCount;
    (void)args;
    return ok(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
}

static NativeResult nativePrint(int argCount, Value *args) {
    for (int i = 0; i < argCount; i++) {
        if (i > 0) printf(" ");
        printValue(args[i]);
    }
    printf("\n");
    return ok(NIL_VAL);
}

static NativeResult nativeLen(int argCount, Value *args) {
    (void)argCount;
    Value v = args[0];
    if (IS_STRING(v)) return ok(NUMBER_VAL(AS_STRING(v)->length));
    if (IS_ARRAY(v)) return ok(NUMBER_VAL(AS_ARRAY(v)->items.count));
    if (IS_MAP(v)) return ok(NUMBER_VAL(AS_MAP(v)->table.count));
    return nativeErrorf("len() expects a string, array or map, got %s.", valueTypeName(v));
}

static NativeResult nativeStr(int argCount, Value *args) {
    (void)argCount;
    char buf[64];
    Value v = args[0];
    if (IS_STRING(v)) return ok(v);
    formatValue(buf, sizeof(buf), v);
    return ok(OBJ_VAL(copyString(buf, (int)strlen(buf))));
}

static NativeResult nativeNum(int argCount, Value *args) {
    (void)argCount;
    Value v = args[0];
    if (IS_NUMBER(v)) return ok(v);
    if (IS_BOOL(v)) return ok(NUMBER_VAL(AS_BOOL(v) ? 1 : 0));
    if (IS_STRING(v)) {
        char *end;
        const char *s = AS_CSTRING(v);
        double d = strtod(s, &end);
        if (end == s || *end != '\0') {
            return nativeErrorf("num() could not convert \"%s\" to a number.", s);
        }
        return ok(NUMBER_VAL(d));
    }
    return nativeErrorf("num() cannot convert a %s.", valueTypeName(v));
}

static NativeResult nativeType(int argCount, Value *args) {
    (void)argCount;
    return ok(OBJ_VAL(copyString(valueTypeName(args[0]), (int)strlen(valueTypeName(args[0])))));
}

static NativeResult nativePush(int argCount, Value *args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) return nativeErrorf("push() expects an array as its first argument.");
    writeValueArray(&AS_ARRAY(args[0])->items, args[1]);
    return ok(args[0]);
}

static NativeResult nativePop(int argCount, Value *args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) return nativeErrorf("pop() expects an array.");
    ObjArray *array = AS_ARRAY(args[0]);
    if (array->items.count == 0) return nativeErrorf("pop() called on an empty array.");
    return ok(array->items.values[--array->items.count]);
}

static NativeResult nativeKeys(int argCount, Value *args) {
    (void)argCount;
    if (!IS_MAP(args[0])) return nativeErrorf("keys() expects a map.");
    ObjMap *map = AS_MAP(args[0]);
    ObjArray *result = newArray();
    push(OBJ_VAL(result));
    for (int i = 0; i < map->table.capacity; i++) {
        Entry *entry = &map->table.entries[i];
        if (entry->key != NULL) {
            writeValueArray(&result->items, OBJ_VAL(entry->key));
        }
    }
    pop();
    return ok(OBJ_VAL(result));
}

static NativeResult nativeHas(int argCount, Value *args) {
    (void)argCount;
    if (!IS_MAP(args[0])) return nativeErrorf("has() expects a map as its first argument.");
    if (!IS_STRING(args[1])) return nativeErrorf("has() expects a string key.");
    Value unused;
    return ok(BOOL_VAL(tableGet(&AS_MAP(args[0])->table, AS_STRING(args[1]), &unused)));
}

static NativeResult nativeAssert(int argCount, Value *args) {
    if (argCount < 1) return nativeErrorf("assert() expects at least 1 argument.");
    bool truthy = !(IS_NIL(args[0]) || (IS_BOOL(args[0]) && !AS_BOOL(args[0])));
    if (truthy) return ok(NIL_VAL);
    if (argCount > 1 && IS_STRING(args[1])) {
        return nativeErrorf("assertion failed: %s", AS_CSTRING(args[1]));
    }
    return nativeErrorf("assertion failed.");
}

static NativeResult nativeSlice(int argCount, Value *args) {
    if (argCount < 2) return nativeErrorf("slice() expects (string, start[, end]).");
    if (!IS_STRING(args[0])) return nativeErrorf("slice() expects a string as its first argument.");
    if (!IS_NUMBER(args[1]) || (argCount > 2 && !IS_NUMBER(args[2]))) {
        return nativeErrorf("slice() expects numeric start/end indices.");
    }
    ObjString *s = AS_STRING(args[0]);
    int start = (int)AS_NUMBER(args[1]);
    int end = argCount > 2 ? (int)AS_NUMBER(args[2]) : s->length;
    if (start < 0) start = 0;
    if (end > s->length) end = s->length;
    if (start > end) start = end;
    return ok(OBJ_VAL(copyString(s->chars + start, end - start)));
}

void registerNatives(void) {
    defineNative("clock", nativeClock, 0);
    defineNative("print", nativePrint, -1);
    defineNative("len", nativeLen, 1);
    defineNative("str", nativeStr, 1);
    defineNative("num", nativeNum, 1);
    defineNative("type", nativeType, 1);
    defineNative("push", nativePush, 2);
    defineNative("pop", nativePop, 1);
    defineNative("keys", nativeKeys, 1);
    defineNative("has", nativeHas, 2);
    defineNative("assert", nativeAssert, -1);
    defineNative("slice", nativeSlice, -1);
}
