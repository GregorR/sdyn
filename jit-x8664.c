/* On x86_64:
 *  All JIT code conforms to the Unix calling convention.
 *
 *  By the Unix calling convention, the first four arguments go in RDI, RSI,
 *  RDX, RCX, the return goes in RAX, RSP is the stack pointer and RBP is the
 *  frame pointer. We use ONLY these registers. RSP must be 16-bit aligned.
 *
 *  RDI is used as the second (collected pointer) stack. RDI will never be
 *  overwritten by a JIT function, but MAY be overwritten by a normal function,
 *  so calls to normal functions must save RDI on the conventional stack (RSP)
 *  and restore it. All functions are guaranteed to preserve RSP and RBP.
 *  Normal functions intended to be called by the JIT take the pointer stack as
 *  their first argument, and so the JIT is never responsible for communicating
 *  it to the GC.
 *
 *  JIT functions themselves take RDI as the pointer stack, RSI as the number
 *  of arguments, and RDX as the argument array. All arguments must be boxed,
 *  and thus RDX is frequently (but not necessarily) a region within the
 *  pointer stack as well. If RSI is 0, RDX may be 0. JIT functions must
 *  restore RDI to its former value before returning to the caller.
 *
 *  When a JIT function initializes, its conventional stack space is not
 *  initialized (i.e., it's garbage), but its pointer stack space must be, and
 *  is initialized to many pointers to sdyn_undefined.
 *
 *  0(RDI) and 8(RDI) are reserved for temporary collected pointer use.
 *  -16(RBP) and -8(RBP) are reserved for temporary non-collected or
 *  non-pointer use. Arguments begin at 16(RDI), and storage begins at
 *  16+x(RDI), where x is the maximum number of arguments times the word size
 *  (8).
 */

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
    SDyn_IRNode node = NULL, unode = NULL, onode = NULL;
    sdyn_native_function_t ret = NULL;
    struct Buffer_uchar buf;
    struct Buffer_size_t returns;
    struct SJA_X8664_Operand left, right, third, target;
    int leftType, rightType, thirdType, targetType;
    size_t i, uidx, lastArg, unsuppCount;
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

    GGC_PUSH_4(ir, node, unode, onode);

    unsuppCount = 0;

    lastArg = 0;
    for (i = 0; i < ir->length; i++) {
        node = GGC_RAP(ir, i);
        unode = node;

        uidx = i;
        while (GGC_RD(unode, uidx) != uidx) {
            uidx = GGC_RD(unode, uidx);
            unode = GGC_RAP(ir, uidx);
        }
        targetType = GGC_RD(unode, rtype);

#define LOADOP(opa, defreg) do { \
    if (GGC_RD(node, opa)) { \
        onode = GGC_RAP(ir, GGC_RD(node, opa)); \
        onode = GGC_RAP(ir, GGC_RD(onode, uidx)); \
        opa ## Type = GGC_RD(onode, rtype); \
        if (GGC_RD(onode, stype) == SDYN_STORAGE_PSTK) { \
            opa = defreg; \
            C2(MOV, defreg, MEM(8, RDI, 0, RNONE, GGC_RD(onode, addr) * 8 + 16)); \
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

        switch (GGC_RD(node, stype)) {
            case SDYN_STORAGE_STK:
                target = MEM(8, RSP, 0, RNONE, GGC_RD(node, addr)*8);
                break;

            case SDYN_STORAGE_ASTK:
            case SDYN_STORAGE_PSTK:
                target = MEM(8, RDI, 0, RNONE, GGC_RD(node, addr)*8 + 16);
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

                imm = GGC_RD(node, imm) * 8 + 16;
                C2(SUB, RDI, IMM(imm));
                IMM64P(RAX, &sdyn_undefined);
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                for (j = 0; j < imm; j += 8)
                    C2(MOV, MEM(8, RDI, 0, RNONE, j), RAX);

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
                imm = GGC_RD(node, imm) * 8 + 16;
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
                C2(LEA, RDX, MEM(8, RDI, 0, RNONE, 16));
                IMM64P(RAX, sdyn_getIntrinsic((SDyn_String) GGC_RP(node, immp)));
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_CALL:
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);
                IMM64P(RAX, sdyn_assertFunction);
                JCALL(RAX);

                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));
                C2(MOV, RDX, IMM(lastArg + 1));
                C2(LEA, RCX, MEM(8, RDI, 0, RNONE, 16));
                IMM64P(RAX, sdyn_call);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_MEMBER:
            {
                SDyn_String *gstring;

                LOADOP(left, RAX);
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

            case SDYN_NODE_ASSIGNMEMBER:
            {
                SDyn_String *gstring;

                LOADOP(left, RAX);
                BOX(leftType, RSI, left);
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);

                LOADOP(right, RAX);
                BOX(rightType, RCX, right);
                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));

                /* make the string globally accessible */
                gstring = (SDyn_String *) createPointer();
                *gstring = GGC_RP(node, immp);
                IMM64P(RDX, gstring);
                C2(MOV, RDX, MEM(8, RDX, 0, RNONE, 0));

                /* get everything into place and call */
                IMM64P(RAX, sdyn_setObjectMember);
                JCALL(RAX);

                C2(MOV, target, RAX);
                break;
            }

            case SDYN_NODE_INDEX:
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);

                LOADOP(right, RAX);
                BOX(rightType, RSI, right);
                IMM64P(RAX, sdyn_toString);
                JCALL(RAX);
                C2(MOV, RDX, RAX);
                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));

                IMM64P(RAX, sdyn_getObjectMember);
                JCALL(RAX);

                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_ASSIGNINDEX:
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);

                LOADOP(right, RAX);
                BOX(rightType, RSI, right);
                IMM64P(RAX, sdyn_toString);
                JCALL(RAX);
                C2(MOV, MEM(8, RDI, 0, RNONE, 8), RAX);

                LOADOP(third, RCX);
                BOX(thirdType, RCX, third);

                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));
                C2(MOV, RDX, MEM(8, RDI, 0, RNONE, 8));

                IMM64P(RAX, sdyn_setObjectMember);
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
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    C2(MOV, RSI, target);
                    IMM64P(RAX, &sdyn_boxInt);
                    JCALL(RAX);
                    C2(MOV, target, RAX);
                }
                break;

            case SDYN_NODE_STR:
            {
                SDyn_String *gstring;

                /* make the string globally accessible */
                gstring = (SDyn_String *) createPointer();
                *gstring = GGC_RP(node, immp);
                *gstring = sdyn_unquote(*gstring);

                /* then simply load it */
                IMM64P(RAX, gstring);
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                C2(MOV, target, RAX);
                break;
            }

            case SDYN_NODE_FALSE:
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    IMM64P(target, &sdyn_false);
                    C2(MOV, target, MEM(8, target, 0, RNONE, 0));
                } else {
                    IMM64(target, 0);
                }
                break;

            case SDYN_NODE_TRUE:
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    IMM64P(target, &sdyn_true);
                    C2(MOV, target, MEM(8, target, 0, RNONE, 0));
                } else {
                    IMM64(target, 1);
                }
                break;

            case SDYN_NODE_OBJ:
                IMM64P(RAX, sdyn_newObject);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            /* Unary: */
            case SDYN_NODE_ARG:
                lastArg = GGC_RD(node, imm);

                /* arguments must be boxed */
                LOADOP(left, RAX);
                BOX(leftType, target, left);
                break;

            case SDYN_NODE_RETURN:
                /* returns must be boxed */
                LOADOP(left, RAX);
                BOX(leftType, RAX, left);

                /* jump to the return address */
                while (BUFFER_SPACE(returns) < 1) EXPAND_BUFFER(returns);
                CF(JMPF, *BUFFER_END(returns));
                returns.bufused++;
                break;

            /* Binary: */

            /* (number, number) -> boolean */
            case SDYN_NODE_LT:
            case SDYN_NODE_GT:
            case SDYN_NODE_LE:
            case SDYN_NODE_GE:
            {
                struct SJA_X8664_Operand intLeft;
                size_t after;

                /* get both operands as numbers */
                intLeft = MEM(8, RBP, 0, RNONE, -8);
                LOADOP(left, RAX);
                switch (leftType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RAX, MEM(8, left, 0, RNONE, 8));
                        C2(MOV, intLeft, RAX);
                        break;

                    case SDYN_TYPE_INT:
                        C2(MOV, intLeft, left);
                        break;

                    default:
                        if (leftType < SDYN_TYPE_FIRST_BOXED) {
                            BOX(leftType, RSI, left);
                        } else {
                            C2(MOV, RSI, left);
                        }
                        IMM64P(RAX, sdyn_toNumber);
                        JCALL(RAX);
                        C2(MOV, intLeft, RAX);
                        break;
                }

                LOADOP(right, RDX);
                switch (rightType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RDX, MEM(8, right, 0, RNONE, 8));
                        break;

                    case SDYN_TYPE_INT:
                        break;

                    default:
                        if (rightType < SDYN_TYPE_FIRST_BOXED) {
                            BOX(rightType, RSI, right);
                        } else {
                            C2(MOV, RSI, right);
                        }
                        IMM64P(RAX, sdyn_toNumber);
                        JCALL(RAX);
                        C2(MOV, RDX, RAX);
                        break;
                }
                C2(MOV, RSI, intLeft);

                /* load true */
                C2(MOV, RAX, IMM(1));

                /* now compare them */
                C2(CMP, RSI, RDX);

                /* do the appropriate jump */
                switch (GGC_RD(node, op)) {
                    case SDYN_NODE_LT: CF(JLF, after); break;
                    case SDYN_NODE_GT: CF(JGF, after); break;
                    case SDYN_NODE_LE: CF(JLEF, after); break;
                    case SDYN_NODE_GE: CF(JGEF, after); break;
                }

                /* load false */
                C2(MOV, RAX, IMM(0));

                /* and return */
                L(after);

                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    C2(MOV, RSI, RAX);
                    IMM64P(RAX, sdyn_boxBool);
                    JCALL(RAX);
                }

                C2(MOV, target, RAX);

                break;
            }

            /* the infamous add */
            case SDYN_NODE_ADD:
            {
                /* this is an infinitely complicated melange of type nonsense */
                LOADOP(left, RAX);
                LOADOP(right, RDX);
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


            /* (number, number) -> number */
            case SDYN_NODE_SUB:
            {
                struct SJA_X8664_Operand intLeft;
                size_t after;

                /* get both operands as numbers */
                intLeft = MEM(8, RBP, 0, RNONE, -8);
                LOADOP(left, RAX);
                switch (leftType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RAX, MEM(8, left, 0, RNONE, 8));
                        C2(MOV, intLeft, RAX);
                        break;

                    case SDYN_TYPE_INT:
                        C2(MOV, intLeft, left);
                        break;

                    default:
                        if (leftType < SDYN_TYPE_FIRST_BOXED) {
                            BOX(leftType, RSI, left);
                        } else {
                            C2(MOV, RSI, left);
                        }
                        IMM64P(RAX, sdyn_toNumber);
                        JCALL(RAX);
                        C2(MOV, intLeft, RAX);
                        break;
                }

                LOADOP(right, RDX);
                switch (rightType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RDX, MEM(8, right, 0, RNONE, 8));
                        break;

                    case SDYN_TYPE_INT:
                        break;

                    default:
                        if (rightType < SDYN_TYPE_FIRST_BOXED) {
                            BOX(rightType, RSI, right);
                        } else {
                            C2(MOV, RSI, right);
                        }
                        IMM64P(RAX, sdyn_toNumber);
                        JCALL(RAX);
                        C2(MOV, RDX, RAX);
                        break;
                }
                C2(MOV, RAX, intLeft);

                /* do the appropriate operation */
                switch (GGC_RD(node, op)) {
                    case SDYN_NODE_SUB: C2(SUB, RAX, RDX); break;
                }

                /* and return */
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    C2(MOV, RSI, RAX);
                    IMM64P(RAX, sdyn_boxInt);
                    JCALL(RAX);
                }
                C2(MOV, target, RAX);

                break;
            }

            case SDYN_NODE_UNIFY: break;

            default:
                fprintf(stderr, "Unsupported operation %s!\n", sdyn_nodeNames[GGC_RD(node, op)]);
                unsuppCount++;
        }
    }

    if (unsuppCount) abort();

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
