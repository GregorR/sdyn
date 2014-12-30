#ifndef SDYN_JIT_H
#define SDYN_JIT_H 1

#include "value.h"

/* compile IR into a native function */
sdyn_native_function_t sdyn_compile(SDyn_IRNodeArray ir);

#endif
