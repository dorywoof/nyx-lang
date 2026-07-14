#include <stdio.h>
#include <string.h>

#include "nyx/object.h"
#include "nyx/table.h"
#include "test_harness.h"

void run_table_tests(void) {
    TEST_SUITE("table");

    Table table;
    initTable(&table);

    ObjString *key1 = copyString("foo", 3);
    ObjString *key2 = copyString("bar", 3);

    Value out;
    TEST_CHECK(!tableGet(&table, key1, &out));

    TEST_CHECK(tableSet(&table, key1, NUMBER_VAL(1)));
    TEST_CHECK(!tableSet(&table, key1, NUMBER_VAL(2)));
    TEST_CHECK(tableGet(&table, key1, &out));
    TEST_CHECK(AS_NUMBER(out) == 2);

    TEST_CHECK(tableSet(&table, key2, BOOL_VAL(true)));
    TEST_CHECK_EQ_INT(table.count, 2);

    TEST_CHECK(tableDelete(&table, key1));
    TEST_CHECK(!tableGet(&table, key1, &out));
    TEST_CHECK(!tableDelete(&table, key1));

    TEST_CHECK(tableSet(&table, key1, NUMBER_VAL(99)));
    TEST_CHECK(tableGet(&table, key1, &out));
    TEST_CHECK(AS_NUMBER(out) == 99);

    Table big;
    initTable(&big);
    char buf[16];
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key%d", i);
        ObjString *k = copyString(buf, (int)strlen(buf));
        tableSet(&big, k, NUMBER_VAL(i));
    }
    TEST_CHECK_EQ_INT(big.count, 200);
    for (int i = 0; i < 200; i++) {
        snprintf(buf, sizeof(buf), "key%d", i);
        ObjString *k = copyString(buf, (int)strlen(buf));
        Value v;
        TEST_CHECK(tableGet(&big, k, &v));
        TEST_CHECK(AS_NUMBER(v) == i);
    }

    ObjString *a = copyString("interned", 8);
    ObjString *b = copyString("interned", 8);
    TEST_CHECK(a == b);

    freeTable(&table);
    freeTable(&big);
}
