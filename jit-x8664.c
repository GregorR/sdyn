/*
 * SDyn: JIT for x86_64. Architecture conventions are described below.
 *
 * Copyright (c) 2015, 2019 Gregor Richards
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

/* On x86_64:
 *  All JIT code conforms to the Unix calling convention.
 *
 *  By the Unix calling convention, the first four arguments go in RDI, RSI,
 *  RDX, RCX, the return goes in RAX, RSP is the stack pointer and RBP is the
 *  frame pointer. We use ONLY these registers. RSP must be 16-byte aligned.
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
 *  non-pointer use. -8(RBP) is generally used to store RDI during calls to
 *  non-JIT functions, and should be avoided in other cases so there is no
 *  accidental overlap. Arguments begin at 16(RDI), and storage begins at
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

/* macros to write pseudo-assembly lines:
 * Cn(opcode, operands) for n-ary assembly instructions
 * CF(opcode, label) for forwards-referencing jumps
 * L(label) to define the label for CF jumps
 * IMM64 to load an immediate value of type size_t
 * IMM64P to load an immediate pointer value */
#define C3(x, o1, o2, o3)   sja_compile(OP3(x, o1, o2, o3), &buf, NULL)
#define C2(x, o1, o2)       sja_compile(OP2(x, o1, o2), &buf, NULL)
#define C1(x, o1)           sja_compile(OP1(x, o1), &buf, NULL)
#define C0(x)               sja_compile(OP0(x), &buf, NULL)
#define CF(x, frel)         sja_compile(OP0(x), &buf, &(frel))
#define IMM64(o1, v) do { \
    size_t imm64 = (v); \
    if (imm64 < 0x100000000L) { \
        C2(MOV, o1, IMM(imm64)); \
    } else if (imm64 & 0x80000000L) { \
        C2(MOV, o1, IMM((~imm64)>>32)); \
        C2(SHL, o1, IMM(32)); \
        C2(XOR, o1, IMM(imm64&0xFFFFFFFFL)); \
    } else { \
        C2(MOV, o1, IMM(imm64>>32)); \
        C2(SHL, o1, IMM(32)); \
        C2(OR, o1, IMM(imm64&0xFFFFFFFFL)); \
    } \
} while(0)
#define IMM64P(o1, v) IMM64(o1, (size_t) (void *) (v))
#define L(frel)             sja_patchFrel(&buf, (frel))

    GGC_PUSH_4(ir, node, unode, onode);

    /* for debugging sake, don't fail on unsupported operations until the end */
    unsuppCount = 0;

    lastArg = 0;
    for (i = 0; i < ir->length; i++) {
        node = GGC_RAP(ir, i);
        unode = node;

        /* find our desired targetType by looking for the unified IR node. Our
         * own rtype SHOULD be identical, but the unified target is the
         * canonical one. */
        uidx = i;
        while (GGC_RD(unode, uidx) != uidx) {
            uidx = GGC_RD(unode, uidx);
            unode = GGC_RAP(ir, uidx);
        }
        targetType = GGC_RD(unode, rtype);

        /* macro to load an operand (left, right, third) into a register */
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

        /* macro to perform a call, saving our pointer stack (see architecture notes at the beginning of this file */
#define JCALL(what) do { \
    C2(MOV, MEM(8, RBP, 0, RNONE, -8), RDI); \
    C1(CALL, what); \
    C2(MOV, RDI, MEM(8, RBP, 0, RNONE, -8)); \
} while(0)

        /* macro to box a value of any type */
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

        /* choose our target based on the storage type */
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
                imm = GGC_RD(node, imm) + 2; /* 2 extra slots for temporaries */
                /* must align stack to 16 by Unix calling conventions */
                if ((imm % 2) != 0) imm++;
                /* 8 bytes per word */
                imm *= 8;

                /* standard entry code */
                C1(PUSH, RBP);
                C2(MOV, RBP, RSP);
                C2(SUB, RSP, IMM(imm));
                break;

            case SDYN_NODE_PALLOCA:
            {
                size_t j;

                imm = GGC_RD(node, imm) * 8 + 16; /* two extra words for temporaries */

                /* explicitly assign sdyn_undefined to all new slots, so all
                 * pointers are valid */
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
                if ((imm % 2) != 0) imm++;
                imm *= 8;
                C2(ADD, RSP, IMM(imm));
                C1(POP, RBP);
                C0(RET);
                break;

            case SDYN_NODE_PPOPA:
            {
                size_t j;
                /* since PPOPA ends the function body, we use this time to fix
                 * up all the forward references */
                for (j = 0; j < returns.bufused; j++)
                    sja_patchFrel(&buf, returns.buf[j]);
                imm = GGC_RD(node, imm) * 8 + 16;
                C2(ADD, RDI, IMM(imm));
                break;
            }

            case SDYN_NODE_IF:
            {
                /* if the condition is false, we will jump to the else clause */
                size_t ifelse;
                LOADOP(left, RAX);

                /* we may need to coerce it */
                if (leftType == SDYN_TYPE_BOXED_BOOL) {
                    C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 8));
                    leftType = SDYN_TYPE_BOOL;
                }
                if (leftType != SDYN_TYPE_BOOL) {
                    BOX(leftType, RSI, RAX);
                    IMM64P(RAX, sdyn_toBoolean);
                    JCALL(RAX);
                }

                C2(CMP, RAX, IMM(0));
                CF(JEF, ifelse);
                GGC_WD(node, imm, ifelse);
                break;
            }

            case SDYN_NODE_IFELSE:
            {
                /* first off, jump out of the if */
                size_t ifelse, ifend;
                CF(JMPF, ifend);
                GGC_WD(node, imm, ifend);

                /* now make the jump in for the else */
                ifelse = GGC_RD(node, left);
                onode = GGC_RAP(ir, ifelse);
                ifelse = GGC_RD(onode, imm);
                L(ifelse);
                break;
            }

            case SDYN_NODE_IFEND:
            {
                /* make the jump out of the if */
                size_t ifend;
                ifend = GGC_RD(node, left);
                onode = GGC_RAP(ir, ifend);
                ifend = GGC_RD(onode, imm);
                L(ifend);
                break;
            }

            case SDYN_NODE_WHILE:
            {
                /* we need to keep track of the program counter at the start of
                 * the while loop, but there is no generated machine code for
                 * it. We just save the PC into the IR node's imm field, which
                 * is otherwise unused */
                size_t wstart;
                wstart = buf.bufused;
                GGC_WD(node, imm, wstart);
                break;
            }

            case SDYN_NODE_WCOND:
            {
                size_t wcond;

                /* first get it to a bool */
                LOADOP(left, RAX);
                if (leftType < SDYN_TYPE_FIRST_BOXED &&
                    leftType != SDYN_TYPE_BOOL) {
                    /* wrong unboxed type! */
                    BOX(leftType, RAX, left);
                    leftType = SDYN_TYPE_BOXED;
                }
                if (leftType >= SDYN_TYPE_FIRST_BOXED) {
                    /* boolify it */
                    C2(MOV, RSI, RAX);
                    IMM64P(RAX, sdyn_toBoolean);
                    JCALL(RAX);
                }

                /* now it's ready to check */
                C2(TEST, RAX, RAX);
                CF(JEF, wcond);

                /* we don't know where to jump to yet, so we save the label in the imm field */
                GGC_WD(node, imm, wcond);
                break;
            }

            case SDYN_NODE_WEND:
            {
                size_t wstart, wcond;

                /* get our wstart and wcond program counters */
                wstart = GGC_RD(node, left);
                onode = GGC_RAP(ir, wstart);
                wstart = GGC_RD(onode, imm);

                wcond = GGC_RD(node, right);
                onode = GGC_RAP(ir, wcond);
                wcond = GGC_RD(onode, imm);

                /* just jump back to the beginning */
                C1(JMPR, RREL(wstart));

                /* then provide the jumping-forward point from the condition */
                L(wcond);

                break;
            }

            case SDYN_NODE_PARAM:
            {
                size_t nonExist;

                /* it is not necessary to provide exactly the right number of
                 * arguments. The number of arguments provided is in RSI. So,
                 * we check whether enough arguments were provided, and if so,
                 * load in an argument value. Because PALLOCA defaults
                 * everything to undefined, we don't have to do anything if
                 * insufficient arguments were provided. */
                C2(CMP, RSI, IMM(GGC_RD(node, imm)));
                CF(JLEF, nonExist); /* argument not provided */
                C2(MOV, RAX, MEM(8, RDX, 0, RNONE, GGC_RD(node, imm)*8)); /* get it from RDX */
                C2(MOV, target, RAX);
                L(nonExist);

                break;
            }

            case SDYN_NODE_INTRINSICCALL:
                /* just get the address of the intrinsic and call it */
                C2(MOV, RSI, IMM(lastArg + 1));
                C2(LEA, RDX, MEM(8, RDI, 0, RNONE, 16));
                IMM64P(RAX, sdyn_getIntrinsic((SDyn_String) GGC_RP(node, immp)));
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_CALL:
                /* left is the function to call, args are handled in ARG nodes */
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);

                /* save the hopefully-function in GC'd space */
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);

                /* assert that it's a function */
                IMM64P(RAX, sdyn_assertFunction);
                JCALL(RAX);

                /* reload it from GC'd space (in case it's moved) */
                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));

                /* pass in the number of arguments */
                C2(MOV, RDX, IMM(lastArg + 1));

                /* ARG loads to RDI+16, so just provide that address as the base for arguments */
                C2(LEA, RCX, MEM(8, RDI, 0, RNONE, 16));

                /* then call sdyn_call */
                IMM64P(RAX, sdyn_call);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_ASSIGN:
                /* assignments don't really exist in IR, so this is just a move, possibly boxing */
                LOADOP(left, RAX);
                if (targetType >= SDYN_TYPE_FIRST_BOXED)
                    BOX(leftType, RAX, left);
                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_MEMBER:
            {
                SDyn_String *gstring;

                LOADOP(left, RAX);
                BOX(leftType, RSI, left);

                if (leftType != SDYN_TYPE_OBJECT) {
                    /* coerce it */
                    IMM64P(RAX, sdyn_toObject);
                    JCALL(RAX);
                    C2(MOV, RSI, RAX);
                }

                /* put the string member name somewhere to load at runtime */
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

                if (leftType != SDYN_TYPE_OBJECT) {
                    /* coerce it */
                    IMM64P(RAX, sdyn_toObject);
                    JCALL(RAX);
                    C2(MOV, RSI, RAX);
                }

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
                /* left is the object to access */
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);

                /* save it in GC'd space */
                C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);

                /* right is the "index", which will be coerced to a string */
                LOADOP(right, RAX);
                BOX(rightType, RSI, right);
                IMM64P(RAX, sdyn_toString);
                JCALL(RAX);
                C2(MOV, RDX, RAX);

                /* reload the object */
                C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));

                /* then simply sdyn_getObjectMember to access */
                IMM64P(RAX, sdyn_getObjectMember);
                JCALL(RAX);

                C2(MOV, target, RAX);
                break;

            case SDYN_NODE_ASSIGNINDEX:
                /* (similar to above, but with a value) */
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

            case SDYN_NODE_SPECULATE:
                LOADOP(left, RSI);

                /* we'll store our label address in imm. Set it to 0 for non-label cases */
                GGC_WD(node, imm, 0);

                /* first off, this is very silly if our input type is already right */
                if (targetType == leftType) {
                    C2(MOV, target, RSI);
                    break;
                }

                /* or if they can't possibly match */
                if (leftType != SDYN_TYPE_BOXED) {
                    if ((leftType == SDYN_TYPE_BOXED_UNDEFINED) && (targetType == SDYN_TYPE_UNDEFINED)) {
                        /* no unboxing required for undefined */

                    } else if (((leftType == SDYN_TYPE_BOXED_BOOL) && (targetType == SDYN_TYPE_BOOL)) ||
                               ((leftType == SDYN_TYPE_BOXED_INT) && (targetType == SDYN_TYPE_INT))) {
                        /* unbox the value */
                        C2(MOV, target, MEM(8, RSI, 0, RNONE, 8));

                    } else if ((leftType == SDYN_TYPE_UNDEFINED) && (targetType == SDYN_TYPE_BOXED_UNDEFINED)) {
                        /* box the undefined value */
                        IMM64P(RAX, &sdyn_undefined);
                        C2(MOV, target, MEM(8, RAX, 0, RNONE, 0));

                    } else if ((leftType == SDYN_TYPE_BOOL) && (targetType == SDYN_TYPE_BOXED_BOOL)) {
                        /* box the bool */
                        IMM64P(RAX, sdyn_boxBool);
                        JCALL(RAX);
                        C2(MOV, target, RAX);

                    } else if ((leftType == SDYN_TYPE_INT) && (targetType == SDYN_TYPE_BOXED_INT)) {
                        /* box the int */
                        IMM64P(RAX, sdyn_boxInt);
                        JCALL(RAX);
                        C2(MOV, target, RAX);

                    } else {
                        /* speculation can never succeed */
                        size_t fail;
                        CF(JMPF, fail);
                        GGC_WD(node, imm, fail);

                    }

                    break;
                }

                /* our input is something boxed; check its type. The type tag is buried like so:
                 * struct Value {
                 *     struct Descriptor *d;
                 *     ...
                 * };
                 * struct Descriptor {
                 *     struct Descriptor *descriptorDescriptor;
                 *     struct Tag *tag;
                 *     ...
                 * };
                 * struct Tag {
                 *     struct Descriptor *tagDescriptor;
                 *     long tag;
                 * };
                 */
                C2(MOV, RAX, MEM(8, RSI, 0, RNONE, 0)); /* get the descriptor */
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 8)); /* get the tag box */
                C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 8)); /* get the tag */

                /* now we check if the tag is what we expect */
                {
                    size_t expected = 0;
                    switch (targetType) {
                        case SDYN_TYPE_UNDEFINED:
                            expected = SDYN_TYPE_BOXED_UNDEFINED;
                            break;

                        case SDYN_TYPE_BOOL:
                            expected = SDYN_TYPE_BOXED_BOOL;
                            break;

                        case SDYN_TYPE_INT:
                            expected = SDYN_TYPE_BOXED_INT;
                            break;

                        default:
                            expected = targetType;
                    }
                    C2(CMP, RAX, IMM(expected));
                }

                /* if it's not, jump to the failed speculation */
                {
                    size_t fail;
                    CF(JNEF, fail);
                    GGC_WD(node, imm, fail);
                }

                break;

            case SDYN_NODE_SPECULATE_FAIL:
            {
                /* our speculation failed. This is the label target for the associated SPECULATE */
                size_t fail;
                fail = GGC_RD(node, left);
                onode = GGC_RAP(ir, fail);
                fail = GGC_RD(onode, imm);
                L(fail);
                break;
            }

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
                    IMM64P(RAX, &sdyn_false);
                    C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                    C2(MOV, target, RAX);
                } else {
                    IMM64(target, 0);
                }
                break;

            case SDYN_NODE_TRUE:
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    IMM64P(RAX, &sdyn_true);
                    C2(MOV, RAX, MEM(8, RAX, 0, RNONE, 0));
                    C2(MOV, target, RAX);
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
                BOX(leftType, RAX, left);
                C2(MOV, target, RAX);
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

            /* (boolean) -> boolean */
            case SDYN_NODE_NOT:
                LOADOP(left, RSI);

                /* do we need to unbox? */
                if (leftType == SDYN_TYPE_BOXED_BOOL) {
                    C2(MOV, RSI, MEM(8, RSI, 0, RNONE, 8));
                    leftType = SDYN_TYPE_BOOL;
                }

                /* do we need to coerce? */
                if (leftType != SDYN_TYPE_BOOL) {
                    IMM64P(RAX, sdyn_toBoolean);
                    JCALL(RAX);
                    C2(MOV, RSI, RAX);
                }

                /* invert */
                C2(XOR, RSI, IMM(1));

                /* and possibly box */
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    IMM64P(RAX, sdyn_boxBool);
                    JCALL(RAX);
                    C2(MOV, target, RAX);
                } else {
                    C2(MOV, target, RSI);
                }
                break;

            case SDYN_NODE_TYPEOF:
                LOADOP(left, RAX);
                BOX(leftType, RSI, left);

                /* just count on sdyn_typeof */
                IMM64P(RAX, sdyn_typeof);
                JCALL(RAX);
                C2(MOV, target, RAX);
                break;

            /* Binary: */

            /* (*, *) -> boolean */
            case SDYN_NODE_EQ:
            case SDYN_NODE_NE:
            {
                LOADOP(left, RSI);
                LOADOP(right, RDX);
                if (leftType != rightType) {
                    /* we can unbox numbers to get compatible types */
                    if (leftType == SDYN_TYPE_INT && rightType == SDYN_TYPE_BOXED_INT) {
                        C2(MOV, right, MEM(8, right, 0, RNONE, 8));
                        rightType = SDYN_TYPE_INT;

                    } else if (leftType == SDYN_TYPE_BOXED_INT && rightType == SDYN_TYPE_INT) {
                        C2(MOV, left, MEM(8, left, 0, RNONE, 8));
                        leftType = SDYN_TYPE_INT;

                    } else if (leftType == SDYN_TYPE_BOXED_INT && rightType == SDYN_TYPE_BOXED_INT) {
                        C2(MOV, left, MEM(8, left, 0, RNONE, 8));
                        C2(MOV, right, MEM(8, right, 0, RNONE, 8));
                        leftType = rightType = SDYN_TYPE_INT;

                    }
                }

                /* we always put our result in RAX, for later moving */
                if (leftType == rightType) {
                    /* if the types are the same, we only need to do a
                     * sophisticated equality comparison if they're both
                     * strings or if we only know they're both boxed */
                    if (leftType == SDYN_TYPE_STRING || leftType == SDYN_TYPE_BOXED) {
                        /* oh well, just use sdyn_equal */
                        IMM64P(RAX, sdyn_equal);
                        JCALL(RAX);

                    } else {
                        size_t eq;

                        /* comparison is direct */
                        C2(MOV, RAX, IMM(1));
                        C2(CMP, left, right);
                        CF(JEF, eq);
                        C2(MOV, RAX, IMM(0));
                        L(eq);

                    }

                } else {
                    /* types aren't the same, so just box and go */
                    BOX(leftType, RSI, RSI);
                    C2(MOV, MEM(8, RDI, 0, RNONE, 0), RSI);
                    LOADOP(right, RDX);
                    BOX(rightType, RDX, RDX);
                    C2(MOV, RSI, MEM(8, RDI, 0, RNONE, 0));
                    IMM64P(RAX, sdyn_equal);
                    JCALL(RAX);

                }

                if (GGC_RD(node, op) == SDYN_NODE_NE) {
                    /* invert our result */
                    C2(XOR, RAX, IMM(1));
                }

                /* possibly box it */
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    C2(MOV, RSI, RAX);
                    IMM64P(RAX, sdyn_boxBool);
                    JCALL(RAX);
                }

                C2(MOV, target, RAX);
                break;
            }

            /* (number, number) -> boolean */
            case SDYN_NODE_LT:
            case SDYN_NODE_GT:
            case SDYN_NODE_LE:
            case SDYN_NODE_GE:
            {
                struct SJA_X8664_Operand intLeft;
                size_t after;

                /* get both operands as numbers */
                intLeft = MEM(8, RBP, 0, RNONE, -16);
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
                }

                LOADOP(right, RDX);
                switch (rightType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RDX, MEM(8, right, 0, RNONE, 8));
                        break;

                    case SDYN_TYPE_INT:
                        C2(MOV, RDX, right);
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

            /* the infamous add, the type of which is:
             * (number, number) -> number
             * (not number, number) -> string
             * (number, not number) -> string
             * (not number, not number) -> string */
            case SDYN_NODE_ADD:
            {
                /* this is an infinitely complicated melange of type nonsense.
                 * We always store our result here in RAX, then just move it to
                 * target at the end. */
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
                    }

                    C2(MOV, target, RAX);

                } else {
                    /* operands are of different types */
                    struct SJA_X8664_Operand boxedLeft;
                    boxedLeft = MEM(8, RDI, 0, RNONE, 0);

                    /* both sides aren't even the same type, so just box 'em and go */
                    if (leftType >= SDYN_TYPE_FIRST_BOXED) {
                        C2(MOV, boxedLeft, left);
                    } else {
                        C2(MOV, RAX, left);
                        BOX(leftType, boxedLeft, RAX);
                        LOADOP(right, RDX);
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

                    C2(MOV, target, RAX);

                }

                break;
            }


            /* (number, number) -> number */
            case SDYN_NODE_SUB:
            case SDYN_NODE_MUL:
            case SDYN_NODE_MOD:
            case SDYN_NODE_DIV:
            {
                struct SJA_X8664_Operand intLeft, result;
                size_t after;

                /* left -> RAX, right -> RSI */

                /* get both operands as numbers */
                intLeft = MEM(8, RBP, 0, RNONE, -16);
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

                LOADOP(right, RSI);
                switch (rightType) {
                    case SDYN_TYPE_BOXED_INT:
                        C2(MOV, RSI, MEM(8, right, 0, RNONE, 8));
                        break;

                    case SDYN_TYPE_INT:
                        break;

                    default:
                        if (rightType < SDYN_TYPE_FIRST_BOXED)
                            BOX(rightType, RSI, right);
                        IMM64P(RAX, sdyn_toNumber);
                        JCALL(RAX);
                        C2(MOV, RSI, RAX);
                        break;
                }
                C2(MOV, RAX, intLeft);

                /* do the appropriate operation */
                switch (GGC_RD(node, op)) {
                    case SDYN_NODE_SUB: C2(SUB, RAX, RSI); result = RAX; break;
                    case SDYN_NODE_MUL: C2(IMUL, RAX, RSI); result = RAX; break;

                    case SDYN_NODE_MOD:
                    case SDYN_NODE_DIV:
                        C2(XOR, RDX, RDX);
                        C1(IDIV, RSI);
                        if (GGC_RD(node, op) == SDYN_NODE_MOD)
                            result = RDX;
                        else
                            result = RAX;
                        break;
                }

                /* and return */
                if (targetType >= SDYN_TYPE_FIRST_BOXED) {
                    C2(MOV, RSI, result);
                    IMM64P(RAX, sdyn_boxInt);
                    JCALL(RAX);
                    C2(MOV, target, RAX);
                } else {
                    C2(MOV, target, result);
                }

                break;
            }

            case SDYN_NODE_NOP:
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
