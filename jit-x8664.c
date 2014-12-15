#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "sdyn/intrinsics.h"
#include "sdyn/nodes.h"
#include "sdyn/value.h"

BUFFER(size_t, size_t);

/* utility function to create a pointer that's GC'd */
static void **createPointer()
{
    void **ret = malloc(sizeof(void *));
    if (ret == NULL) {
        perror("malloc");
        abort();
    }

    *ret = NULL;
    GGC_PUSH_1(*ret);
    GGC_GLOBALIZE();

    return ret;
}

/* compile IR into a native function */
sdyn_native_function_t sdyn_compile(SDyn_IRNodeArray ir)
{
    SDyn_IRNode node = NULL, onode = NULL;
    sdyn_native_function_t ret = NULL;
    struct Buffer_uchar buf;
    struct Buffer_size_t returns;
    struct SJA_X8664_Operand left, right, third, target;
    int leftType, rightType, thirdType;
    size_t i, lastArg;
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
    if (imm64 < 0x100000000L) { \
        C2(MOV, o1, IMM(imm64)); \
    } else { \
        C2(MOV, o1, IMM(imm64>>32)); \
        C2(SHL, o1, IMM(32)); \
        C2(OR, o1, IMM(imm64&0xFFFFFFFFL)); \
    } \
} while(0)
#define L(frel)             sja_patchFrel(&buf, (frel))

    GGC_PUSH_3(ir, node, onode);

    lastArg = 0;
    for (i = 0; i < ir->length; i++) {
        node = GGC_RAP(ir, i);

#define LOADOP(opa, defreg) do { \
    if (GGC_RD(node, opa)) { \
        onode = GGC_RAP(ir, GGC_RD(node, opa)); \
        opa ## Type = GGC_RD(onode, rtype); \
        onode = GGC_RAP(ir, GGC_RD(onode, uidx)); \
        if (GGC_RD(onode, stype) == SDYN_STORAGE_PSTK) { \
            opa = defreg; \
            C2(MOV, defreg, MEM(RDI, 0, RNONE, GGC_RD(onode, addr) * 8)); \
        } else if (GGC_RD(onode, stype) == SDYN_STORAGE_STK) { \
            opa = defreg; \
            C2(MOV, defreg, MEM(RSP, 0, RNONE, GGC_RD(onode, addr) * 8)); \
        } \
    } \
} while(0)
#define JCALL(what) do { \
    C2(MOV, MEM(RBP, 0, RNONE, -8), RDI); \
    C1(CALL, what); \
    C2(MOV, RDI, MEM(RBP, 0, RNONE, -8)); \
} while(0)
#define BOX(type, targ, reg) do { \
    switch (type) { \
        case SDYN_TYPE_UNDEFINED: \
            IMM64(RAX, (size_t) (void *) &sdyn_undefined); \
            C2(MOV, targ, MEM(RAX, 0, RNONE, 0)); \
            break; \
            \
        case SDYN_TYPE_BOOL: \
            C2(MOV, RSI, reg); \
            IMM64(RAX, (size_t) (void *) sdyn_boxBool); \
            JCALL(RAX); \
            C2(MOV, targ, RAX); \
            break; \
            \
        case SDYN_TYPE_INT: \
            C2(MOV, RSI, reg); \
            IMM64(RAX, (size_t) (void *) sdyn_boxInt); \
            JCALL(RAX); \
            C2(MOV, targ, RAX); \
            break; \
            \
        default: \
            C2(MOV, targ, reg); \
    } \
} while(0)

        LOADOP(left, RAX);
        LOADOP(right, RDX);

        switch (GGC_RD(node, stype)) {
            case SDYN_STORAGE_STK:
                target = MEM(RSP, 0, RNONE, GGC_RD(node, addr)*8);
                break;

            case SDYN_STORAGE_ASTK:
            case SDYN_STORAGE_PSTK:
                target = MEM(RDI, 0, RNONE, GGC_RD(node, addr)*8);
                break;

            default:
                target = RAX;
        }

        switch (GGC_RD(node, op)) {
            case SDYN_NODE_ALLOCA:
                imm = GGC_RD(node, imm) + 2;
                /* must align stack to 16 */
                if ((imm % 2) == 1) imm++;
                imm *= 8;
                C2(ENTER, IMM(imm), IMM(0));
                break;

            case SDYN_NODE_PALLOCA:
                imm = GGC_RD(node, imm) * 8;
                C2(SUB, RDI, IMM(imm));
                break;

            case SDYN_NODE_POPA:
                C0(LEAVE);
                C0(RET);
                break;

            case SDYN_NODE_PPOPA:
            {
                size_t j;
                /* fix up all the forward references */
                for (j = 0; j < returns.bufused; j++)
                    sja_patchFrel(&buf, returns.buf[j]);
                imm = GGC_RD(node, imm) * 8;
                C2(ADD, RDI, IMM(imm));
                break;
            }

            case SDYN_NODE_INTRINSICCALL:
                C2(MOV, RSI, IMM(lastArg + 1));
                C2(LEA, RDX, MEM(RDI, 0, RNONE, 0));
                IMM64(RAX, (size_t) (void *) sdyn_getIntrinsic((SDyn_String) GGC_RP(node, immp)));
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_CALL:
                BOX(leftType, RSI, left);
                IMM64(RAX, (size_t) (void *) sdyn_assertFunction);
                JCALL(RAX);

                BOX(leftType, RSI, left);
                C2(MOV, RDX, IMM(lastArg + 1));
                C2(LEA, RCX, MEM(RDI, 0, RNONE, 0));
                IMM64(RAX, (size_t) (void *) sdyn_call);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            /* 0-ary: */
            case SDYN_NODE_TOP:
                IMM64(RAX, (size_t) (void *) &sdyn_globalObject);
                C2(MOV, RAX, MEM(RAX, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_NIL:
                IMM64(target, (size_t) (void *) &sdyn_undefined);
                C2(MOV, RAX, MEM(target, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_NUM:
                C2(MOV, target, IMM(GGC_RD(node, imm)));
                break;

            /* Unary: */
            case SDYN_NODE_ARG:
                lastArg = GGC_RD(node, imm);

                /* arguments must be boxed */
                BOX(leftType, target, left);
                break;

            case SDYN_NODE_RETURN:
                /* returns must be boxed */
                BOX(leftType, RAX, left);

                /* jump to the return address */
                while (BUFFER_SPACE(returns) < 1) EXPAND_BUFFER(returns);
                CF(JMPF, *BUFFER_END(returns));
                returns.bufused++;
                break;

            case SDYN_NODE_MEMBER:
            {
                SDyn_String *gstring;

                BOX(leftType, RSI, left);

                /* make the string globally accessible */
                gstring = (SDyn_String *) createPointer();
                *gstring = GGC_RP(node, immp);
                IMM64(RDX, (size_t) (void *) gstring);
                C2(MOV, RDX, MEM(RDX, 0, RNONE, 0));

                /* get everything into place and call */
                IMM64(RAX, (size_t) (void *) sdyn_getObjectMember);
                JCALL(RAX);

                C2(MOV, target, RAX);
                break;
            }

            default:
                fprintf(stderr, "Unsupported operation %s!\n", sdyn_nodeNames[GGC_RD(node, op)]);
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
