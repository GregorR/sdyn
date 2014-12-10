#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sdyn/intrinsics.h"

/* get an intrinsic by name */
sdyn_native_function_t sdyn_getIntrinsic(SDyn_String intrinsic)
{
    GGC_char_Array schar = NULL;

    GGC_PUSH_2(intrinsic, schar);

    schar = GGC_RP(intrinsic, value);

#define TOK(str) if (!strncmp(schar->a__data, "$" #str, schar->length))

    TOK(print) {
        return sdyn_iPrint;
    }

    fprintf(stderr, "Invalid native function %.*s!\n", schar->length, schar->a__data);
    abort();
}

SDyn_Undefined sdyn_iPrint(void **pstack, size_t argCt, SDyn_Undefined *args)
{
    SDyn_String string = NULL;
    GGC_char_Array schar = NULL;

    if (pstack) ggc_jitPointerStack = pstack;

    GGC_PUSH_2(string, schar);

    if (argCt < 1) return sdyn_undefined;

    string = sdyn_toString(NULL, args[0]);
    schar = GGC_RP(string, value);
    printf("%.*s\n", schar->length, schar->a__data);

    return sdyn_undefined;
}
