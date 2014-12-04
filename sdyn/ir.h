#ifndef SDYN_IR_H
#define SDYN_IR_H 1

#include "ggggc/gc.h"

#include "nodes.h"
#include "parser.h"

/* the various places values may be stored */
enum SDyn_StorageType {
    SDYN_STORAGE_NIL,
    SDYN_STORAGE_REG,
    SDYN_STORAGE_STK, /* self-allocated stack space */
    SDYN_STORAGE_ASTK, /* argument stack space */
    SDYN_STORAGE_LAST
};

/* a map for the register allocator to determine which space is usable */
struct SDyn_RegisterMap {
    size_t count;
    unsigned char usable[1];
};

GGC_TYPE(SDyn_IRNode)
    GGC_MDATA(int, op);
    GGC_MDATA(int, rtype); /* result type of this operation */
    GGC_MDATA(int, stype); /* the storage type in which to place the result */
    GGC_MDATA(size_t, addr); /* the address this value is assigned to */

    /* Operands: */
    GGC_MDATA(long, imm); /* any immediate operand */
    GGC_MPTR(void *, immp); /* any pointer immediate operand */
    GGC_MDATA(size_t, left); /* the left operand */
    GGC_MDATA(size_t, right); /* the right operand */
    GGC_MDATA(size_t, third); /* the third operand, if applicable */
GGC_END_TYPE(SDyn_IRNode,
    GGC_PTR(SDyn_IRNode, immp)
    );

/* compile a function to IR */
SDyn_IRNodeArray sdyn_irCompilePrime(SDyn_Node func);

/* perform register allocation on an IR */
void sdyn_irRegAlloc(SDyn_IRNodeArray ir, struct SDyn_RegisterMap *registerMap);

/* compile and perform register allocation */
SDyn_IRNodeArray sdyn_irCompile(SDyn_Node func, struct SDyn_RegisterMap *registerMap);

#endif
