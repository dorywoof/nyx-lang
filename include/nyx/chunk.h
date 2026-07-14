#ifndef NYX_CHUNK_H
#define NYX_CHUNK_H

#include "nyx/common.h"
#include "nyx/opcodes.h"
#include "nyx/value.h"

/*
 * A Chunk is one function body compiled to bytecode: a flat byte array plus
 * a parallel line-number array (same index, so code[i] was emitted from
 * source line lines[i]) and a constant pool for literals too big to fit in
 * an opcode's operand byte.
 */
typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);

#endif
