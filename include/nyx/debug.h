#ifndef NYX_DEBUG_H
#define NYX_DEBUG_H

#include "nyx/chunk.h"

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

#endif
