#ifndef NYX_COMPILER_H
#define NYX_COMPILER_H

#include "nyx/object.h"

ObjFunction *compile(const char *source);

void markCompilerRoots(void);

#endif
