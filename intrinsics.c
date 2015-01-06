/*
 * SDyn: Implementation of intrinsic functions
 *
 * Copyright (c) 2015 Gregor Richards
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sdyn/exec.h"
#include "sdyn/intrinsics.h"

/* get an intrinsic by name. All intrinsics are simply hardwired */
sdyn_native_function_t sdyn_getIntrinsic(SDyn_String intrinsic)
{
    GGC_char_Array schar = NULL;

    GGC_PUSH_2(intrinsic, schar);

    schar = GGC_RP(intrinsic, value);

#define TOK(str) if (!strncmp(schar->a__data, "$" #str, schar->length))

    TOK(eval) {
        return sdyn_iEval;
    } else TOK(print) {
        return sdyn_iPrint;
    }

    fprintf(stderr, "Invalid native function %.*s!\n", (int) schar->length, schar->a__data);
    abort();
}

/* global eval */
SDyn_Undefined sdyn_iEval(void **pstack, size_t argCt, SDyn_Undefined *args)
{
    SDyn_String codeStr = NULL;
    GGC_char_Array codeA = NULL;
    unsigned char *code;

    if (pstack) ggc_jitPointerStack = pstack;

    GGC_PUSH_2(codeStr, codeA);

    /* get our code */
    if (argCt >= 1) codeStr = sdyn_toString(NULL, args[0]);
    else codeStr = sdyn_boxString(NULL, "", 0);

    /* get it out of the GC */
    codeA = GGC_RP(codeStr, value);
    code = malloc(codeA->length + 1);
    if (!code) {
        perror("malloc");
        exit(1);
    }
    memcpy(code, codeA->a__data, codeA->length);
    code[codeA->length] = 0;

    /* and execute */
    sdyn_exec(code);

    return sdyn_undefined;
}

/* print a value of any type, by coercing it to a string */
SDyn_Undefined sdyn_iPrint(void **pstack, size_t argCt, SDyn_Undefined *args)
{
    SDyn_String string = NULL;
    GGC_char_Array schar = NULL;

    if (pstack) ggc_jitPointerStack = pstack;

    GGC_PUSH_2(string, schar);

    if (argCt < 1) return sdyn_undefined;

    string = sdyn_toString(NULL, args[0]);
    schar = GGC_RP(string, value);
    printf("%.*s\n", (int) schar->length, schar->a__data);

    return sdyn_undefined;
}
