#ifndef NYX_COMPILER_H
#define NYX_COMPILER_H

#include "nyx/object.h"

/* Compiles source straight to a top-level ObjFunction (the implicit
 * "script" function whose chunk is the whole program). Returns NULL on a
 * syntax error (errors are already printed to stderr by the parser). */
ObjFunction *compile(const char *source);

/* GC root marking for objects only reachable from an in-progress compile
 * (e.g. a nested function being compiled that isn't on the VM stack yet). */
void markCompilerRoots(void);

#endif
