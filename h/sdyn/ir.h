/*
 * SDyn: IR-related functionality, including types.
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

#ifndef SDYN_IR_H
#define SDYN_IR_H 1

#include "ggggc/gc.h"

#include "nodes.h"
#include "parser.h"

/* the various places values may be stored */
enum SDyn_StorageType {
    SDYN_STORAGE_NIL,
    SDYN_STORAGE_REG,
    SDYN_STORAGE_STK, /* normal (data) stack space */
    SDYN_STORAGE_ASTK, /* argument stack space (in practice identical to PSTK) */
    SDYN_STORAGE_PSTK, /* pointer stack space */
    SDYN_STORAGE_LAST
};

/* a map for the register allocator to determine which space is usable */
struct SDyn_RegisterMap {
    size_t count;
    unsigned char usable[1];
};

GGC_TYPE(SDyn_IRNode)
    /* Operation */
    GGC_MDATA(int, op);
    GGC_MDATA(int, rtype); /* result type of this operation */

    /* Operands: */
    GGC_MDATA(long, imm); /* any immediate operand */
    GGC_MPTR(void *, immp); /* any pointer immediate operand */
    GGC_MDATA(size_t, left); /* the left operand */
    GGC_MDATA(size_t, right); /* the right operand */
    GGC_MDATA(size_t, third); /* the third operand, if applicable */

    /* Register allocation: */
    GGC_MDATA(int, stype); /* the storage type in which to place the result */
    GGC_MDATA(size_t, addr); /* the address this value is assigned to */
    GGC_MDATA(size_t, uidx); /* the index after unification */
    GGC_MPTR(GGC_size_t_Array, lastUsed); /* values used no later than here */
GGC_END_TYPE(SDyn_IRNode,
    GGC_PTR(SDyn_IRNode, immp)
    GGC_PTR(SDyn_IRNode, lastUsed)
    );

/* compile a function to IR */
SDyn_IRNodeArray sdyn_irCompilePrime(SDyn_Node func);

/* perform register allocation on an IR */
void sdyn_irRegAlloc(SDyn_IRNodeArray ir, struct SDyn_RegisterMap *registerMap);

/* compile and perform register allocation */
SDyn_IRNodeArray sdyn_irCompile(SDyn_Node func, struct SDyn_RegisterMap *registerMap);

#endif
