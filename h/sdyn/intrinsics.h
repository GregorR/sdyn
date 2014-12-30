#ifndef SDYN_INTRINSICS_H
#define SDYN_INTRINSICS_H 1

#include "tokenizer.h"
#include "value.h"

/* get an intrinsic by name */
sdyn_native_function_t sdyn_getIntrinsic(SDyn_String intrinsic);

SDyn_Undefined sdyn_iPrint(void **pstack, size_t argCt, SDyn_Undefined *args);

#endif
