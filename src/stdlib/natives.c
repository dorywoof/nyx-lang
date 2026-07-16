#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nyx/memory.h"
#include "nyx/natives.h"
#include "nyx/object.h"
#include "nyx/vm.h"

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

static NativeResult nativeFind(int argCount, Value *args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return nativeErrorf("find() expects (string, string).");
    }
    ObjString *haystack = AS_STRING(args[0]);
    ObjString *needle = AS_STRING(args[1]);
    if (needle->length == 0) return ok(NUMBER_VAL(0));
    for (int i = 0; i + needle->length <= haystack->length; i++) {
        if (memcmp(haystack->chars + i, needle->chars, (size_t)needle->length) == 0) {
            return ok(NUMBER_VAL(i));
        }
    }
    return ok(NUMBER_VAL(-1));
}

static NativeResult nativeSplit(int argCount, Value *args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return nativeErrorf("split() expects (string, separator).");
    }
    ObjString *s = AS_STRING(args[0]);
    ObjString *sep = AS_STRING(args[1]);
    if (sep->length == 0) return nativeErrorf("split() expects a non-empty separator.");
    ObjArray *result = newArray();
    push(OBJ_VAL(result));
    int start = 0;
    int i = 0;
    while (i + sep->length <= s->length) {
        if (memcmp(s->chars + i, sep->chars, (size_t)sep->length) == 0) {
            ObjString *piece = copyString(s->chars + start, i - start);
            push(OBJ_VAL(piece));
            writeValueArray(&result->items, OBJ_VAL(piece));
            pop();
            i += sep->length;
            start = i;
        } else {
            i++;
        }
    }
    ObjString *tail = copyString(s->chars + start, s->length - start);
    push(OBJ_VAL(tail));
    writeValueArray(&result->items, OBJ_VAL(tail));
    pop();
    pop();
    return ok(OBJ_VAL(result));
}

static NativeResult nativeJoin(int argCount, Value *args) {
    (void)argCount;
    if (!IS_ARRAY(args[0]) || !IS_STRING(args[1])) {
        return nativeErrorf("join() expects (array, separator).");
    }
    ObjArray *array = AS_ARRAY(args[0]);
    ObjString *sep = AS_STRING(args[1]);
    size_t total = 0;
    for (int i = 0; i < array->items.count; i++) {
        if (!IS_STRING(array->items.values[i])) {
            return nativeErrorf("join() expects an array of strings, got %s at index %d.",
                                valueTypeName(array->items.values[i]), i);
        }
        total += (size_t)AS_STRING(array->items.values[i])->length;
        if (i > 0) total += (size_t)sep->length;
    }
    char *buf = malloc(total + 1);
    if (buf == NULL) return nativeErrorf("join() ran out of memory.");
    size_t at = 0;
    for (int i = 0; i < array->items.count; i++) {
        if (i > 0) {
            memcpy(buf + at, sep->chars, (size_t)sep->length);
            at += (size_t)sep->length;
        }
        ObjString *item = AS_STRING(array->items.values[i]);
        memcpy(buf + at, item->chars, (size_t)item->length);
        at += (size_t)item->length;
    }
    ObjString *joined = copyString(buf, (int)total);
    free(buf);
    return ok(OBJ_VAL(joined));
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
    defineNative("find", nativeFind, 2);
    defineNative("split", nativeSplit, 2);
    defineNative("join", nativeJoin, 2);
}
