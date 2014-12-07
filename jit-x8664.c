#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "sdyn/nodes.h"
#include "sdyn/value.h"

BUFFER(size_t, size_t);

/* compile IR into a native function */
sdyn_native_function_t sdyn_compile(SDyn_IRNodeArray ir)
{
    SDyn_IRNode node = NULL, onode = NULL;
    sdyn_native_function_t ret = NULL;
    struct Buffer_uchar buf;
    struct Buffer_size_t returns;
    struct SJA_X8664_Operand left, right, third, target;
    size_t i;
    long imm;

    INIT_BUFFER(buf);
    INIT_BUFFER(returns);

#define C3(x, o1, o2, o3)   sja_compile(OP3(x, o1, o2, o3), &buf, NULL)
#define C2(x, o1, o2)       sja_compile(OP2(x, o1, o2), &buf, NULL)
#define C1(x, o1)           sja_compile(OP1(x, o1), &buf, NULL)
#define C0(x)               sja_compile(OP0(x), &buf, NULL)
#define CF(x, frel)         sja_compile(OP0(x), &buf, &(frel))
#define IMM64(o1, v) do { \
    size_t imm64 = (v); \
    C2(MOV, o1, IMM(imm64>>32)); \
    C2(SHL, o1, IMM(32)); \
    C2(OR, o1, IMM(imm64&0xFFFFFFFFL)); \
} while(0)
#define L(frel)             sja_patchFrel(&buf, (frel))

    GGC_PUSH_3(ir, node, onode);

    for (i = 0; i < ir->length; i++) {
        node = GGC_RAP(ir, i);

#define LOADOP(opa, defreg) do { \
    if (GGC_RD(node, opa)) { \
        onode = GGC_RAP(ir, GGC_RD(node, opa)); \
        onode = GGC_RAP(ir, GGC_RD(onode, uidx)); \
        if (GGC_RD(onode, stype) == SDYN_STORAGE_STK) { \
            opa = defreg; \
            C2(MOV, defreg, MEM(RSP, 0, RNONE, GGC_RD(onode, addr) * 8)); \
        } \
    } \
} while(0)

        LOADOP(left, RAX);
        LOADOP(right, RDX);

        target = RAX;

        switch (GGC_RD(node, op)) {
            case SDYN_NODE_ALLOCA:
                imm = GGC_RD(node, imm);
                /* must align stack to 16 */
                if ((imm % 2) == 0) imm++;
                imm *= 8;
                C2(ENTER, IMM(imm), IMM(0));
                break;

            case SDYN_NODE_POPA:
            {
                size_t j;
                /* fix up all the forward references */
                for (j = 0; j < returns.bufused; j++)
                    sja_patchFrel(&buf, returns.buf[j]);
                C0(LEAVE);
                C0(RET);
                break;
            }

            /* 0-ary: */
            case SDYN_NODE_NIL:
                IMM64(target, (size_t) (void *) &sdyn_undefined);
                C2(MOV, target, MEM(target, 0, RNONE, 0));
                break;

            case SDYN_NODE_NUM:
                C2(MOV, target, IMM(GGC_RD(node, imm)));
                break;

            /* Unary: */
            case SDYN_NODE_ARG:
                /* arguments must be boxed (FIXME: things other than ints) */
                C2(MOV, RSI, left);
                IMM64(RAX, (size_t) (void *) sdyn_boxInt);
                C1(CALL, RAX);
                break;

            case SDYN_NODE_RETURN:
                C2(MOV, RAX, left);
                /* jump to the return address */
                while (BUFFER_SPACE(returns) < 1) EXPAND_BUFFER(returns);
                CF(JMPF, *BUFFER_END(returns));
                returns.bufused++;
                break;

            case SDYN_NODE_INTRINSICCALL:
            case SDYN_NODE_CALL:

            default:
                fprintf(stderr, "Unsupported operation %s!\n", sdyn_nodeNames[GGC_RD(node, op)]);
        }

        if (GGC_RD(node, stype) == SDYN_STORAGE_STK) {
            C2(MOV, MEM(RSP, 0, RNONE, GGC_RD(node, addr)*8), target);
        }
    }

    /* now transfer it to executable memory */
    {
        size_t sz = (buf.bufused + 4095) / 4096 * 4096;
        unsigned char *retMap;

        retMap = mmap(NULL, sz, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON, -1, 0);
        memcpy(retMap, buf.buf, buf.bufused);
        ret = (sdyn_native_function_t) retMap;
    }

    FREE_BUFFER(buf);
    FREE_BUFFER(returns);

    return ret;
}
