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
    int leftType, rightType, thirdType, targetType;
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
#define IMM64P(o1, v) IMM64(o1, (size_t) (void *) (v))
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
            C2(MOV, defreg, MEM(8, RDI, 0, RNONE, GGC_RD(onode, addr) * 8)); \
        } else if (GGC_RD(onode, stype) == SDYN_STORAGE_STK) { \
            opa = defreg; \
            C2(MOV, defreg, MEM(8, RSP, 0, RNONE, GGC_RD(onode, addr) * 8)); \
        } \
    } \
} while(0)
#define JCALL(what) do { \
    C2(MOV, MEM(8, RBP, 0, RNONE, -8), RDI); \
    C1(CALL, what); \
    C2(MOV, RDI, MEM(8, RBP, 0, RNONE, -8)); \
} while(0)
#define BOX(type, targ, reg) do { \
    switch (type) { \
        case SDYN_TYPE_UNDEFINED: \
            IMM64P(RAX, &sdyn_undefined); \
            C2(MOV, targ, MEM(8, RAX, 0, RNONE, 0)); \
            break; \
            \
        case SDYN_TYPE_BOOL: \
            C2(MOV, RSI, reg); \
            IMM64P(RAX, sdyn_boxBool); \
            JCALL(RAX); \
            C2(MOV, targ, RAX); \
            break; \
            \
        case SDYN_TYPE_INT: \
            C2(MOV, RSI, reg); \
            IMM64P(RAX, sdyn_boxInt); \
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
        targetType = GGC_RD(node, rtype);

        switch (GGC_RD(node, stype)) {
            case SDYN_STORAGE_STK:
                target = MEM(8, RSP, 0, RNONE, GGC_RD(node, addr)*8);
                break;

            case SDYN_STORAGE_ASTK:
            case SDYN_STORAGE_PSTK:
                target = MEM(8, RDI, 0, RNONE, GGC_RD(node, addr)*8);
                break;

            default:
                target = RAX;
        }

        switch (GGC_RD(node, op)) {
            case SDYN_NODE_ALLOCA:
                imm = GGC_RD(node, imm) + 2;
                /* must align stack to 16 */
                if ((imm % 2) == 0) imm++;
                imm *= 8;
                C1(PUSH, RBP);
                C2(MOV, RBP, RSP);
                C2(SUB, RSP, IMM(imm));
                break;

            case SDYN_NODE_PALLOCA:
            {
                size_t j;

                imm = GGC_RD(node, imm) * 8;
                if (imm > 0) {
                    C2(SUB, RDI, IMM(imm));
                    IMM64P(RAX, &sdyn_undefined);
                    C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                    for (j = 0; j < imm; j += 8)
                        C2(MOV, MEM(8, RDI, 0, RNONE, j), RAX);
                }

                break;
            }

            case SDYN_NODE_POPA:
                imm = GGC_RD(node, imm) + 2;
                /* must align stack to 16 */
                if ((imm % 2) == 0) imm++;
                imm *= 8;
                C2(ADD, RSP, IMM(imm));
                C1(POP, RBP);
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

            case SDYN_NODE_PARAM:
            {
                size_t nonExist;

                /* because PALLOCA defaults everything to undefined, we don't
                 * have to default the value. Just check if it's OK */
                C2(CMP, RSI, IMM(GGC_RD(node, imm)));
                CF(JLEF, nonExist); /* argument not provided */
                C2(MOV, RAX, MEM(8, RDX, 0, RNONE, GGC_RD(node, imm)*8)); /* get it from RDX */
                C2(MOV, target, RAX);
                L(nonExist);

                break;
            }

            case SDYN_NODE_INTRINSICCALL:
                C2(MOV, RSI, IMM(lastArg + 1));
                C2(LEA, RDX, MEM(8, RDI, 0, RNONE, 0));
                IMM64P(RAX, sdyn_getIntrinsic((SDyn_String) GGC_RP(node, immp)));
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_CALL:
                BOX(leftType, RSI, left);
                IMM64P(RAX, sdyn_assertFunction);
                JCALL(RAX);

                BOX(leftType, RSI, left);
                C2(MOV, RDX, IMM(lastArg + 1));
                C2(LEA, RCX, MEM(8, RDI, 0, RNONE, 0));
                IMM64P(RAX, sdyn_call);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            /* 0-ary: */
            case SDYN_NODE_TOP:
                IMM64P(RAX, &sdyn_globalObject);
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_NIL:
                IMM64P(target, &sdyn_undefined);
                C2(MOV, RAX, MEM(8, target, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_NUM:
                C2(MOV, target, IMM(GGC_RD(node, imm)));
                break;

            case SDYN_NODE_STR:
            {
                SDyn_String *gstring;

                /* make the string globally accessible */
                gstring = (SDyn_String *) createPointer();
                *gstring = GGC_RP(node, immp);

                /* then simply load it */
                IMM64P(RAX, gstring);
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;
            }

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
                IMM64P(RDX, gstring);
                C2(MOV, RDX, MEM(8, RDX, 0, RNONE, 0));

                /* get everything into place and call */
                IMM64P(RAX, sdyn_getObjectMember);
                JCALL(RAX);

                C2(MOV, target, RAX);
                break;
            }

            /* Binary: */
            case SDYN_NODE_ADD:
            {
                /* this is an infinitely complicated melange of type nonsense */
                if (leftType == rightType) {
                    /* "easier" case: They're at least the same type */
                    switch (leftType) {
                        case SDYN_TYPE_UNDEFINED:
                            /* let the generic case handle it */
                            IMM64P(RSI, &sdyn_undefined);
                            C2(MOV, RSI, MEM(8, RSI, 0, RNONE, 0));
                            C2(MOV, RDX, RSI);
                            IMM64P(RAX, sdyn_add);
                            JCALL(RAX);
                            break;

                        case SDYN_TYPE_BOOL:
                            /* box them and then go to the generic case */
                            C2(MOV, RSI, left);
                            IMM64P(RAX, sdyn_boxBool);
                            JCALL(RAX);
                            C2(MOV, MEM(8, RDI, 0, RNONE, 0), RAX); /* remember boxed left */
                            C2(MOV, RSI, right);
                            IMM64P(RAX, sdyn_boxBool);
                            JCALL(RAX);

                            /* put them in the argument slots */
                            C2(MOV, RDX, RAX);
                            C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));

                            /* and add */
                            IMM64P(RAX, sdyn_add);
                            JCALL(RAX);
                            break;

                        case SDYN_TYPE_INT:
                            if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                                /* may as well box now */
                                C2(MOV, RSI, left);
                                C2(ADD, RSI, right);
                                IMM64P(RAX, sdyn_boxInt);
                                JCALL(RAX);

                            } else {
                                /* just add! */
                                C2(MOV, RAX, left);
                                C2(ADD, RAX, right);

                            }
                            break;

                        case SDYN_TYPE_BOXED_INT:
                        {
                            struct SJA_X8664_Operand tmpTarget = RAX;
                            if (targetType >= SDYN_TYPE_FIRST_BOXED) tmpTarget = RSI;

                            /* the only boxed case we actually care to unbox */
                            C2(MOV, tmpTarget, left);
                            C2(MOV, tmpTarget, MEM(8, RAX, 0, RNONE, 8));
                            C2(MOV, RDX, right);
                            C2(ADD, tmpTarget, MEM(8, RDX, 0, RNONE, 8));

                            /* rebox the result if asked */
                            if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                                IMM64P(RAX, sdyn_boxInt);
                                JCALL(RAX);
                            }
                            break;
                        }

                        default:
                            /* something boxed, just count on the generic adder */
                            C2(MOV, RSI, left);
                            C2(MOV, RDX, right);
                            IMM64P(RAX, sdyn_add);
                            JCALL(RAX);
                            break;
                    }

                    C2(MOV, target, RAX);

                } else {
                    struct SJA_X8664_Operand boxedLeft;

                    /* both sides aren't even the same type, so just box 'em and go */
                    if (leftType >= SDYN_TYPE_FIRST_BOXED) {
                        boxedLeft = left;
                    } else {
                        boxedLeft = MEM(8, RDI, 0, RNONE, 0);
                        C2(MOV, RAX, left);
                        BOX(leftType, RAX, RAX);
                        C2(MOV, boxedLeft, RAX);
                    }
                    if (rightType >= SDYN_TYPE_FIRST_BOXED) {
                        C2(MOV, RDX, right);
                    } else {
                        C2(MOV, RAX, right);
                        BOX(rightType, RDX, RAX);
                    }
                    C2(MOV, RSI, boxedLeft);
                    IMM64P(RAX, sdyn_add);
                    JCALL(RAX);

                }

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
