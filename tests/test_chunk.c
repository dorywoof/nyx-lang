#include "nyx/chunk.h"
#include "nyx/vm.h"
#include "test_harness.h"

void run_chunk_tests(void) {
    TEST_SUITE("chunk");

    Chunk chunk;
    initChunk(&chunk);
    TEST_CHECK_EQ_INT(chunk.count, 0);

    for (int i = 0; i < 20; i++) {
        writeChunk(&chunk, (uint8_t)i, i);
    }
    TEST_CHECK_EQ_INT(chunk.count, 20);
    TEST_CHECK(chunk.capacity >= 20);
    for (int i = 0; i < 20; i++) {
        TEST_CHECK_EQ_INT(chunk.code[i], i);
        TEST_CHECK_EQ_INT(chunk.lines[i], i);
    }

    int idx0 = addConstant(&chunk, NUMBER_VAL(3.5));
    int idx1 = addConstant(&chunk, NUMBER_VAL(7.0));
    TEST_CHECK_EQ_INT(idx0, 0);
    TEST_CHECK_EQ_INT(idx1, 1);
    TEST_CHECK(AS_NUMBER(chunk.constants.values[0]) == 3.5);
    TEST_CHECK(AS_NUMBER(chunk.constants.values[1]) == 7.0);

    freeChunk(&chunk);
    TEST_CHECK_EQ_INT(chunk.count, 0);
    TEST_CHECK_EQ_INT(chunk.capacity, 0);
}
