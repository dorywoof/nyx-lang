#ifndef NYX_TABLE_H
#define NYX_TABLE_H

#include "nyx/common.h"
#include "nyx/value.h"

/*
 * Open-addressing hash table, keyed by interned ObjString* (pointer
 * equality is valid because the string interner in vm.c guarantees one
 * ObjString per distinct character sequence). This one structure backs
 * three different language-level things: the VM's global-variable table,
 * the string interner itself, and every user-level map object -- see
 * docs/architecture.md "why one hash table implementation" for why that
 * reuse was worth the coupling.
 */
typedef struct {
    ObjString *key; /* NULL means empty slot */
    Value value;
} Entry;

typedef struct {
    int count;    /* live entries, including tombstones */
    int capacity;
    Entry *entries;
} Table;

void initTable(Table *table);
void freeTable(Table *table);

bool tableGet(Table *table, ObjString *key, Value *value);
/* Returns true if this added a *new* key (false if it overwrote one). */
bool tableSet(Table *table, ObjString *key, Value value);
bool tableDelete(Table *table, ObjString *key);
void tableAddAll(Table *from, Table *to);

/* Used only by the string interner to find an existing ObjString by raw
 * characters before allocating a new one. */
ObjString *tableFindString(Table *table, const char *chars, int length, uint32_t hash);

/* GC support: drop entries whose key is unmarked (used for the intern
 * table, which must not be a GC root or it would leak every string). */
void tableRemoveWhiteKeys(Table *table);
void markTable(Table *table);

#endif
