#ifndef NYX_COMMON_H
#define NYX_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Uncomment via CMake -DNYX_STRESS_GC=ON: collect before every allocation. */
/* #define NYX_STRESS_GC */

/* Uncomment via CMake -DNYX_GC_LOG=ON: print every GC decision to stderr. */
/* #define NYX_LOG_GC */

#define NYX_MAX_CALL_FRAMES 256
#define NYX_MAX_STACK (NYX_MAX_CALL_FRAMES * 256)

#endif
