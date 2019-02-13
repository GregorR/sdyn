/*
 * SDyn: IR-related functionality, including compiling parse trees into IR, and
 * doing "register allocation" over IR. Note that this implementation has no
 * real register allocation; everything goes in memory.
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

/*
 * SDyn's IR is in SSA form. SSA instructions are "Static Single Assignment":
 * i.e., each SSA instruction produces a value, and in general that value may
 * never be changed. It is up to register allocation to determine how much
 * space those values must occupy, and when memory locations may be reused.
 *
 * IR instructions refer to each other in terms of indexes within an array.
 * Each IR instruction has as many as three such indexes, labeled 'left',
 * 'right' and 'third'. Their meanings are documented in h/sdyn/nodex.h .
 *
 * IR instructions additionally have a type, which refers to the type of value
 * which will be generated at runtime. This type must be semantically correct;
 * i.e., the JIT is free to assume that the IR will never be written to insist
 * that a number be typed as a string, or that object members will be unboxed,
 * etc. The most important use for this type is to enforce that values are
 * boxed by the JIT.
 *
 * To support variables which may change differently in different branches,
 * like any SSA, SDyn's IR uses unification (also known as the Phi function).
 * The UNIFY operation indicates that two values are to be assigned to the same
 * location by the register allocator, effectively making them a single
 * variable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc/collections/list.h"

#include "sdyn/ir.h"
#include "sdyn/value.h"

GGC_LIST(SDyn_IRNode)

/* utility function to clone a symbol table */
static SDyn_IndexMap cloneSymbolTable(SDyn_IndexMap symbols)
{
    SDyn_IndexMap symbols2 = NULL;
    SDyn_String name = NULL;
    GGC_size_t_Unit indexBox = NULL;
    size_t i;

    GGC_PUSH_4(symbols, symbols2, name, indexBox);

    /* we'll need to compare our symbol table before and after to unify, so first, copy */
    symbols2 = SDyn_IndexMapClone(symbols);

    return symbols2;
}

/* utility function to unify symbol tables */
static void unifySymbolTables(SDyn_IRNodeList ir, SDyn_IndexMap symbols, SDyn_IndexMap symbols2, int loop)
{
    SDyn_IndexMapEntry entry = NULL;
    SDyn_IRNode irn = NULL;
    SDyn_String name = NULL;
    GGC_size_t_Unit indexBox = NULL, indexBox2 = NULL;
    size_t i;

    GGC_PUSH_8(ir, symbols, symbols2, entry, irn, name, indexBox, indexBox2);

    /* go through each symbol */
    for (i = 0; i < GGC_RD(symbols2, size); i++) {
        entry = GGC_RAP(GGC_RP(symbols2, entries), i);
        while (entry) {
            size_t idx;
            name = GGC_RP(entry, key);
            indexBox2 = GGC_RP(entry, value);
            idx = GGC_RD(indexBox2, v);
            if (SDyn_IndexMapGet(symbols, name, &indexBox)) {
                if (indexBox != indexBox2) {
                    size_t idx;

                    /* they both have different indexes, must unify */
                    irn = GGC_NEW(SDyn_IRNode);
                    GGC_WD(irn, op, SDYN_NODE_UNIFY);
                    GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                    idx = GGC_RD(indexBox, v);
                    GGC_WD(irn, left, idx);
                    idx = GGC_RD(indexBox2, v);
                    GGC_WD(irn, right, idx);
                    indexBox = GGC_NEW(GGC_size_t_Unit);
                    idx = GGC_RD(ir, length);
                    GGC_WD(indexBox, v, idx);
                    SDyn_IRNodeListPush(ir, irn);
                }

            } else {
                /* added in symbols2, just need to copy it */
                SDyn_IndexMapPut(symbols, name, indexBox2);

            }

            /* NOP it so it stays alive for loops */
            if (loop) {
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_NOP);
                GGC_WD(irn, left, idx);
                SDyn_IRNodeListPush(ir, irn);
            }

            entry = GGC_RP(entry, next);
        }
    }

    return;
}

/* compile a parse tree node to IR */
static size_t irCompileNode(SDyn_IRNodeList ir, SDyn_Node node, SDyn_IndexMap symbols, size_t *target)
{
    SDyn_NodeArray children = NULL;
    SDyn_Node cnode = NULL;
    SDyn_IRNode irn = NULL;
    SDyn_String name = NULL;
    GGC_size_t_Unit indexBox = NULL, indexBox2 = NULL;
    GGC_size_t_Array args = NULL;
    SDyn_IndexMap symbols2 = NULL;

    struct SDyn_Token tok;
    size_t i;

    GGC_PUSH_11(ir, node, symbols, children, cnode, irn, name, indexBox, indexBox2, args, symbols2);

    children = GGC_RP(node, children);

#define SUB(x) irCompileNode(ir, GGC_RAP(children, x), symbols, NULL)
#define IRNNEW() do { \
    int irntype; \
    irn = GGC_NEW(SDyn_IRNode); \
    irntype = GGC_RD(node, type); \
    GGC_WD(irn, op, irntype); \
} while(0)

    switch (GGC_RD(node, type)) {
        case SDYN_NODE_TOP:
        case SDYN_NODE_GLOBALCALL:
            /* only for debugging purposes */
            if (children)
                for (i = 0; i < children->length; i++)
                    SUB(i);
            break;

        case SDYN_NODE_VARDECLS:
        case SDYN_NODE_STATEMENTS:
            /* simple list */
            for (i = 0; i < children->length; i++)
                SUB(i);
            break;

        case SDYN_NODE_FUNDECL:
            /* our top level */
            /* make space for locals */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_ALLOCA);
            SDyn_IRNodeListPush(ir, irn);
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_PALLOCA);
            SDyn_IRNodeListPush(ir, irn);

            SUB(0); /* params */
            SUB(1); /* vardecls */
            SUB(2); /* statements */

            /* return undefined */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_NIL);
            GGC_WD(irn, rtype, SDYN_TYPE_UNDEFINED);
            i = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_RETURN);
            GGC_WD(irn, left, i);
            SDyn_IRNodeListPush(ir, irn);

            /* pop our space */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_PPOPA);
            SDyn_IRNodeListPush(ir, irn);
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_POPA);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_PARAMS:
            /* first the "this" parameter */
            name = sdyn_boxString(NULL, "this", 4);

            /* add it to the symbol table */
            indexBox = GGC_NEW(GGC_size_t_Unit);
            i = GGC_RD(ir, length);
            GGC_WD(indexBox, v, i);
            SDyn_IndexMapPut(symbols, name, indexBox);

            /* make the IR node */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_PARAM);
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            GGC_WD(irn, imm, 0);
            SDyn_IRNodeListPush(ir, irn);

            /* now the normal parameters */
            for (i = 0; i < children->length; i++) {
                size_t paramIdx, paramNum;

                cnode = GGC_RAP(children, i);

                tok = GGC_RD(cnode, tok);
                name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);

                /* add it to the symbol table */
                indexBox = GGC_NEW(GGC_size_t_Unit);
                paramIdx = GGC_RD(ir, length);
                GGC_WD(indexBox, v, paramIdx);
                SDyn_IndexMapPut(symbols, name, indexBox);

                /* make the IR node */
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_PARAM);
                GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                paramNum = i + 1;
                GGC_WD(irn, imm, paramNum);

                /* add it to the list */
                SDyn_IRNodeListPush(ir, irn);
            }
            break;

        case SDYN_NODE_VARDECL:
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);

            /* add it to the symbol table */
            indexBox = GGC_NEW(GGC_size_t_Unit);
            i = GGC_RD(ir, length);
            GGC_WD(indexBox, v, i);
            SDyn_IndexMapPut(symbols, name, indexBox);

            /* and make an IR node */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_NIL);
            GGC_WD(irn, rtype, SDYN_TYPE_UNDEFINED);

            /* add it to the list */
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_ASSIGN:
            /* what we do from here depends on the type of the LHS */
            cnode = GGC_RAP(children, 0);
            switch (GGC_RD(cnode, type)) {
                case SDYN_NODE_INDEX:
                    irn = GGC_NEW(SDyn_IRNode);
                    GGC_WD(irn, op, SDYN_NODE_ASSIGNINDEX);
                    GGC_WD(irn, rtype, SDYN_TYPE_BOXED);

                    /* get the indexed object */
                    children = GGC_RP(cnode, children);
                    i = SUB(0);
                    GGC_WD(irn, left, i);

                    /* get the index itself */
                    i = SUB(1);
                    GGC_WD(irn, right, i);
                    children = GGC_RP(node, children);

                    /* get the value */
                    i = SUB(1);
                    GGC_WD(irn, third, i);

                    /* and perform the assignment */
                    SDyn_IRNodeListPush(ir, irn);

                    break;

                case SDYN_NODE_MEMBER:
                    irn = GGC_NEW(SDyn_IRNode);
                    GGC_WD(irn, op, SDYN_NODE_ASSIGNMEMBER);
                    GGC_WD(irn, rtype, SDYN_TYPE_BOXED);

                    /* get the object */
                    children = GGC_RP(cnode, children);
                    i = SUB(0);
                    GGC_WD(irn, left, i);
                    children = GGC_RP(node, children);

                    /* the name is the token */
                    tok = GGC_RD(cnode, tok);
                    name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
                    GGC_WP(irn, immp, name);

                    /* get the value */
                    i = SUB(1);
                    GGC_WD(irn, right, i);

                    /* and perform the assignment */
                    SDyn_IRNodeListPush(ir, irn);

                    break;

                case SDYN_NODE_VARREF:
                {
                    size_t val;

                    /* get the value */
                    val = SUB(1);

                    /* the variable being accessed */
                    tok = GGC_RD(cnode, tok);
                    name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);

                    /* check if it's in the symbol table */
                    if (SDyn_IndexMapGet(symbols, name, &indexBox)) {
                        /* local variable reference */
                        IRNNEW();
                        GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                        GGC_WD(irn, left, val);
                        val = GGC_RD(ir, length);
                        SDyn_IRNodeListPush(ir, irn);

                        /* update the symbol table */
                        indexBox = GGC_NEW(GGC_size_t_Unit);
                        GGC_WD(indexBox, v, val);
                        SDyn_IndexMapPut(symbols, name, indexBox);

                    } else {
                        /* global variable reference, perform as assignment on global object */
                        size_t g;

                        /* get the global object */
                        irn = GGC_NEW(SDyn_IRNode);
                        GGC_WD(irn, op, SDYN_NODE_TOP);
                        GGC_WD(irn, rtype, SDYN_TYPE_OBJECT);
                        g = GGC_RD(ir, length);
                        SDyn_IRNodeListPush(ir, irn);

                        /* and perform the assignment */
                        irn = GGC_NEW(SDyn_IRNode);
                        GGC_WD(irn, op, SDYN_NODE_ASSIGNMEMBER);
                        GGC_WD(irn, left, g);
                        GGC_WD(irn, right, val);
                        GGC_WP(irn, immp, name);
                        SDyn_IRNodeListPush(ir, irn);
                    }

                    break;
                }

                default:
                    fprintf(stderr, "Invalid assignment to %s!\n", sdyn_nodeNames[GGC_RD(cnode, type)]);
                    abort();
            }
            break;

        case SDYN_NODE_VARREF:
        {
            size_t g;

            /* just get it out of the symbol table */
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            if (SDyn_IndexMapGet(symbols, name, &indexBox))
                return GGC_RD(indexBox, v);

            /* not in the local symbol table, must be a global */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_TOP);
            GGC_WD(irn, rtype, SDYN_TYPE_OBJECT);
            g = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* do a member lookup */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_MEMBER);
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            GGC_WD(irn, left, g);
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name);
            SDyn_IRNodeListPush(ir, irn);

            break;
        }

        case SDYN_NODE_IF:
        {
            SDyn_IndexMap symbolsSwap;
            size_t nodeIf, nodeElse;

            /* check the condition */
            i = SUB(0);

            /* we'll need to unify both sides of the if */
            symbols2 = cloneSymbolTable(symbols);

            /* conditionally jump to else */
            IRNNEW();
            GGC_WD(irn, left, i);
            nodeIf = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* do the if body */
            SUB(1);

            /* begin the else */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_IFELSE);
            GGC_WD(irn, left, nodeIf);
            nodeElse = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* swap our symbol tables */
            symbolsSwap = symbols;
            symbols = symbols2;
            symbols2 = symbolsSwap;

            /* do the else body */
            if (GGC_RAP(children, 2))
                SUB(2);

            /* then the end */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_IFEND);
            GGC_WD(irn, left, nodeElse);
            SDyn_IRNodeListPush(ir, irn);

            /* and unify */
            unifySymbolTables(ir, symbols, symbols2, 0);

            break;
        }

        case SDYN_NODE_WHILE:
        {
            size_t begin, cond;

            /* mark the beginning */
            IRNNEW();
            begin = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* we'll need to compare our symbol table before and after to unify, so first, copy */
            symbols2 = cloneSymbolTable(symbols);

            /* now do the while condition */
            i = SUB(0); /* NOTE: actually need to unify here to be correct */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_WCOND);
            GGC_WD(irn, left, i);
            cond = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* the loop body */
            SUB(1);

            /* and the loop end */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_WEND);
            GGC_WD(irn, left, begin);
            GGC_WD(irn, right, cond);
            SDyn_IRNodeListPush(ir, irn);

            /* then unify our pre-loop and post-loop variables */
            unifySymbolTables(ir, symbols, symbols2, 1);
            break;
        }

        case SDYN_NODE_MEMBER:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);

            /* get the object */
            i = SUB(0);
            if (target) *target = i;
            GGC_WD(irn, left, i);

            /* the name is the token */
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name);

            SDyn_IRNodeListPush(ir, irn);

            break;

        case SDYN_NODE_INDEX:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);

            /* get the object */
            i = SUB(0);
            if (target) *target = i;
            GGC_WD(irn, left, i);

            /* and the index */
            i = SUB(1);
            GGC_WD(irn, right, i);

            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_CALL:
        {
            size_t f, target;

            /* get the target and function to call */
            target = 0;
            cnode = GGC_RAP(children, 0);
            f = irCompileNode(ir, cnode, symbols, &target);

            /* make room for argument values */
            cnode = GGC_RAP(children, 1);
            children = GGC_RP(cnode, children);
            args = GGC_NEW_DA(size_t, children->length + 1);

            /* set the target argument */
            if (!target) {
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_NIL);
                GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                target = GGC_RD(ir, length);
                SDyn_IRNodeListPush(ir, irn);
            }
            GGC_WAD(args, 0, target);

            /* evaluate all the arguments */
            for (i = 0; i < children->length; i++) {
                size_t v, anum;

                v = SUB(i);
                anum = i + 1;
                GGC_WAD(args, anum, v);

            }

            /* put them in argument slots */
            for (i = 0; i < args->length; i++) {
                size_t v;
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_ARG);
                v = GGC_RAD(args, i);
                GGC_WD(irn, left, v);
                GGC_WD(irn, imm, i);
                SDyn_IRNodeListPush(ir, irn);
            }

            /* now perform the call */
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            GGC_WD(irn, left, f);
            SDyn_IRNodeListPush(ir, irn);

            break;
        }

        case SDYN_NODE_INTRINSICCALL:
        {
            /* make room for arguments */
            cnode = GGC_RAP(children, 0);
            children = GGC_RP(cnode, children);
            args = GGC_NEW_DA(size_t, children->length);

            /* evaluate them */
            for (i = 0; i < children->length; i++) {
                size_t v;

                v = SUB(i);
                GGC_WAD(args, i, v);
            }

            /* put them in arg slots */
            for (i = 0; i < args->length; i++) {
                size_t v;

                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_ARG);
                v = GGC_RAD(args, i);
                GGC_WD(irn, left, v);
                GGC_WD(irn, imm, i);
                SDyn_IRNodeListPush(ir, irn);
            }

            /* now perform the call */
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            GGC_WD(irn, imm, i);
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name);
            SDyn_IRNodeListPush(ir, irn);

            break;
        }

        /* 0-ary nodes: */
        case SDYN_NODE_NUM:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_INT);
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            {
                long v = sdyn_toNumber(NULL, (SDyn_Undefined) name);
                GGC_WD(irn, imm, v);
            }
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_STR:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_STRING);
            tok = GGC_RD(node, tok);
            name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name); /* FIXME */
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_FALSE:
        case SDYN_NODE_TRUE:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOOL);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_OBJ:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_OBJECT);
            SDyn_IRNodeListPush(ir, irn);
            break;

        /* unary nodes: */
        case SDYN_NODE_RETURN:
            IRNNEW();
            i = SUB(0);
            GGC_WD(irn, left, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_NOT:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOOL);
            i = SUB(0);
            GGC_WD(irn, left, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_TYPEOF:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_STRING);
            i = SUB(0);
            GGC_WD(irn, left, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        /* binary nodes: */
        case SDYN_NODE_OR:
        case SDYN_NODE_AND:
        {
            size_t cond1, cond1n, cond2, ifNode, ifElse;

            /* compile the first condition */
            cond1 = SUB(0);

            /* we need not-condition because or's the opposite case */
            if (GGC_RD(node, type) == SDYN_NODE_OR) {
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_NOT);
                GGC_WD(irn, rtype, SDYN_TYPE_BOOL);
                GGC_WD(irn, left, cond1);
                cond1n = GGC_RD(ir, length);
                SDyn_IRNodeListPush(ir, irn);
            } else {
                cond1n = cond1;
            }

            /* compile it as an if */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_IF);
            GGC_WD(irn, left, cond1n);
            ifNode = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* the second condition is optional, so need to unify */
            symbols2 = cloneSymbolTable(symbols);

            /* get the second condition */
            cond2 = SUB(1);

            /* end the if */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_IFELSE);
            GGC_WD(irn, left, ifNode);
            ifElse = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_IFEND);
            GGC_WD(irn, left, ifElse);
            SDyn_IRNodeListPush(ir, irn);

            /* then unify */
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_UNIFY);
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            GGC_WD(irn, left, cond1);
            GGC_WD(irn, right, cond2);
            SDyn_IRNodeListPush(ir, irn);
            unifySymbolTables(ir, symbols, symbols2, 0);
            break;
        }

        case SDYN_NODE_EQ:
        case SDYN_NODE_NE:
        case SDYN_NODE_LT:
        case SDYN_NODE_GT:
        case SDYN_NODE_LE:
        case SDYN_NODE_GE:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOOL);
            i = SUB(0);
            GGC_WD(irn, left, i);
            i = SUB(1);
            GGC_WD(irn, right, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_ADD:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            i = SUB(0);
            GGC_WD(irn, left, i);
            i = SUB(1);
            GGC_WD(irn, right, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_SUB:
        case SDYN_NODE_MUL:
        case SDYN_NODE_MOD:
        case SDYN_NODE_DIV:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_INT);
            i = SUB(0);
            GGC_WD(irn, left, i);
            i = SUB(1);
            GGC_WD(irn, right, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        default:
            fprintf(stderr, "Unsupported node %s! (%.*s)\n",
                sdyn_nodeNames[GGC_RD(node, type)], (int) GGC_RD(node, tok).valLen, GGC_RD(node, tok).val);
            abort();
    }

#undef SUB
#undef IRNNEW

    /* with no other return known, we assume the last IR node is the result */
    return GGC_RD(ir, length) - 1;
}

/* set up the uidxs for all nodes */
static void irUidx(SDyn_IRNodeArray ir)
{
    SDyn_IRNode node = NULL, unode = NULL;
    ssize_t si;
    size_t idx, uidx;

    GGC_PUSH_3(ir, node, unode);

    /* first off, default uidxs */
    for (si = ir->length - 1; si >= 0; si--) {
        node = GGC_RAP(ir, si);
        GGC_WD(node, uidx, si);
    }

    /* then unify */
    for (si = ir->length - 1; si >= 0; si--) {
        node = GGC_RAP(ir, si);

        if (GGC_RD(node, op) == SDYN_NODE_UNIFY) {
            idx = GGC_RD(node, uidx);
            GGC_WD(node, rtype, SDYN_TYPE_BOXED);
            unode = GGC_RAP(ir, GGC_RD(node, left));
            GGC_WD(unode, uidx, idx);
            unode = GGC_RAP(ir, GGC_RD(node, right));
            GGC_WD(unode, uidx, idx);
        }
    }
}

/* flow IR types through operations */
static void irFlowTypes(SDyn_IRNodeArray ir)
{
    SDyn_IRNode node = NULL, unode = NULL, onode = NULL;
    int changed;
    int leftType, rightType, thirdType, origTargetType, targetType;
    size_t i, uidx;

    GGC_PUSH_4(ir, node, unode, onode);

    do {
        changed = 0;
        for (i = 0; i < ir->length; i++) {
            /* get the node and its unification target */
            node = GGC_RAP(ir, i);
            unode = node;
            uidx = i;
            while (GGC_RD(unode, uidx) != uidx) {
                uidx = GGC_RD(unode, uidx);
                unode = GGC_RAP(ir, uidx);
            }
            targetType = GGC_RD(unode, rtype);

            /* get all its operands */
#define OPTYPE(op) do { \
    uidx = GGC_RD(node, op); \
    onode = GGC_RAP(ir, uidx); \
    while (GGC_RD(onode, uidx) != uidx) { \
        uidx = GGC_RD(onode, uidx); \
        onode = GGC_RAP(ir, uidx); \
    } \
    op ## Type = GGC_RD(onode, rtype); \
} while(0)
            OPTYPE(left);
            OPTYPE(right);
            OPTYPE(third);
#undef OPTYPE
            origTargetType = targetType = GGC_RD(node, rtype);

            /* then choose the result type */
            switch (GGC_RD(node, op)) {
                case SDYN_NODE_ASSIGN:
                    /* just an alias */
                    targetType = leftType;
                    break;

                case SDYN_NODE_ASSIGNMEMBER:
                    /* alias with an assignment */
                    targetType = rightType;
                    break;

                case SDYN_NODE_ASSIGNINDEX:
                    /* alias with an index */
                    targetType = thirdType;
                    break;

                case SDYN_NODE_ADD:
                    /* in some specific cases, we can predict the result type */
                    if ((leftType == SDYN_TYPE_INT || leftType == SDYN_TYPE_BOXED_INT) &&
                            (rightType == SDYN_TYPE_INT || rightType == SDYN_TYPE_BOXED_INT)) {
                        /* both ints, result is int */
                        targetType = SDYN_TYPE_INT;

                    } else if (leftType != SDYN_TYPE_BOXED && rightType != SDYN_TYPE_BOXED) {
                        /* both types are known, but they're not both ints, so the result is a string */
                        targetType = SDYN_TYPE_STRING;

                    } else if ((leftType == SDYN_TYPE_BOXED && rightType != SDYN_TYPE_BOXED && rightType != SDYN_TYPE_INT && rightType != SDYN_TYPE_BOXED_INT) ||
                               (rightType == SDYN_TYPE_BOXED && leftType != SDYN_TYPE_BOXED && leftType != SDYN_TYPE_INT && rightType != SDYN_TYPE_BOXED_INT)) {
                        /* one side or the other is not an int, so the result must be a string */
                        targetType = SDYN_TYPE_STRING;

                    }
                    break;

                case SDYN_NODE_UNIFY:
                    /* if both sides are the same type, then the unification needn't be blind boxing */
                    if (leftType == rightType) {
                        targetType = leftType;

                    } else if ((leftType == SDYN_TYPE_BOOL && rightType == SDYN_TYPE_BOXED_BOOL) ||
                            (leftType == SDYN_TYPE_BOXED_BOOL && rightType == SDYN_TYPE_BOOL)) {
                        targetType = SDYN_TYPE_BOXED_BOOL;

                    } else if ((leftType == SDYN_TYPE_INT && rightType == SDYN_TYPE_BOXED_INT) ||
                            (leftType == SDYN_TYPE_BOXED_INT && rightType == SDYN_TYPE_INT)) {
                        targetType = SDYN_TYPE_BOXED_INT;

                    }
                    break;
            }

            if (origTargetType != targetType) {
                /* we chose a more precise type */
                GGC_WD(node, rtype, targetType);
                changed = 1;
            }
        }
    } while(changed);
}

/* compile a function to IR */
SDyn_IRNodeArray sdyn_irCompilePrime(SDyn_Node func)
{
    SDyn_IRNodeList ir = NULL;
    SDyn_IRNodeArray ret = NULL;
    SDyn_IndexMap symbols = NULL;

    GGC_PUSH_4(func, ir, ret, symbols);

    /* compile it */
    ir = GGC_NEW(SDyn_IRNodeList);
    symbols = GGC_NEW(SDyn_IndexMap);
    irCompileNode(ir, func, symbols, NULL);

    /* convert to array */
    ret = SDyn_IRNodeListToArray(ir);

    /* do type propagation */
    irUidx(ret);
    irFlowTypes(ret);

    return ret;
}

/* perform register allocation on an IR */
void sdyn_irRegAlloc(SDyn_IRNodeArray ir, struct SDyn_RegisterMap *registerMap)
{
    SDyn_IRNode node = NULL, unode = NULL, callNode = NULL;
    GGC_char_Array stksUsed = NULL, pstksUsed = NULL, irUsed = NULL;
    GGC_size_t_Array lastUsed = NULL;
    int last[4];
    int li, tmpi;
    size_t i, idx, stkUsed, pstkUsed, astkUsed;
    long si;

    GGC_PUSH_8(ir, node, unode, callNode, stksUsed, pstksUsed, irUsed, lastUsed);

#define USED(v) do { \
    size_t vv = (v); \
    unode = GGC_RAP(ir, vv); \
    idx = GGC_RD(unode, uidx); \
    if (vv && !GGC_RAD(irUsed, idx)) { \
        /* it's used here and wasn't already used, so this must be the last use */ \
        last[li++] = vv; \
        GGC_WAD(irUsed, idx, 1); \
    } \
} while(0)

    irUsed = GGC_NEW_DA(char, ir->length);

    /* then perform last-use analysis */
    for (si = ir->length - 1; si >= 0; si--) {
        node = GGC_RAP(ir, si);

        li = 0;
        USED(GGC_RD(node, uidx));
        USED(GGC_RD(node, left));
        USED(GGC_RD(node, right));
        USED(GGC_RD(node, third));

        /* handle special cases */
        switch (GGC_RD(node, op)) {
            case SDYN_NODE_CALL:
            case SDYN_NODE_INTRINSICCALL:
                /* calls need to associate all their args, but to do that, we'll need to wait 'til the last arg */
                callNode = node;
                break;

            case SDYN_NODE_ARG:
                /* an argument for a call. If callNode isn't set, this is a mistake! */
                if (callNode) {
                    lastUsed = GGC_RP(callNode, lastUsed);
                    if (!lastUsed) {
                        /* it doesn't have a lastUsed yet, so we must be the last argument */
                        lastUsed = GGC_NEW_DA(size_t, GGC_RD(node, imm) + 3);
                        GGC_WP(callNode, lastUsed, lastUsed);

                        /* set the call's dependencies */
                        idx = GGC_RD(callNode, uidx);
                        GGC_WAD(lastUsed, 0, idx);
                        if (GGC_RD(callNode, op) == SDYN_NODE_CALL) {
                            /* it also has a left */
                            unode = GGC_RAP(ir, GGC_RD(callNode, left));
                            idx = GGC_RD(unode, uidx);
                            GGC_WAD(lastUsed, 1, idx);
                        }

                        idx = GGC_RD(node, uidx);
                    }

                    /* add ourself to the last used of the call */
                    GGC_WAD(lastUsed, GGC_RD(node, imm) + 2, idx);
                }
                /* no break */

            default:
                if (li) {
                    /* set its lastUsed */
                    lastUsed = GGC_NEW_DA(size_t, li);
                    for (li--; li >= 0; li--) {
                        tmpi = last[li];
                        GGC_WAD(lastUsed, li, tmpi);
                    }
                    GGC_WP(node, lastUsed, lastUsed);
                }
        }
    }

#undef USED

    /* now do simple "register" assignment */
    stksUsed = GGC_NEW_DA(char, ir->length);
    pstksUsed = GGC_NEW_DA(char, ir->length);
    stkUsed = pstkUsed = astkUsed = 0;
    for (si = 0; si < ir->length; si++) {
        int stype = 0;
        size_t addr = 0;
        GGC_char_Array *cstksUsed;
        size_t *cstkUsed;

        node = GGC_RAP(ir, si);
        idx = GGC_RD(node, uidx);
        unode = GGC_RAP(ir, idx);
        while (GGC_RD(unode, uidx) != idx) {
            idx = GGC_RD(unode, uidx);
            unode = GGC_RAP(ir, idx);
        }

        /* special cases */
        if (GGC_RD(node, op) == SDYN_NODE_ARG) {
            stype = SDYN_STORAGE_ASTK;
            addr = GGC_RD(node, imm);
            if (addr >= astkUsed) astkUsed = addr + 1;
            GGC_WD(node, stype, stype);
            GGC_WD(node, addr, addr);
            GGC_WD(unode, stype, stype);
            GGC_WD(unode, addr, addr);
            continue;
        }

        /* does this even need a register? */
        if (GGC_RD(node, rtype) == SDYN_TYPE_NIL) continue;

        /* has it already been assigned? */
        if (GGC_RD(unode, stype)) {
            stype = GGC_RD(unode, stype);
            addr = GGC_RD(unode, addr);
            GGC_WD(node, stype, stype);
            GGC_WD(node, addr, addr);
            continue;
        }

        /* does it need to go on the pointer stack? */
        if (GGC_RD(unode, rtype) >= SDYN_TYPE_FIRST_BOXED) {
            stype = SDYN_STORAGE_PSTK;
            cstksUsed = &pstksUsed;
            cstkUsed = &pstkUsed;
        } else {
            stype = SDYN_STORAGE_STK;
            cstksUsed = &stksUsed;
            cstkUsed = &stkUsed;
        }

        /* find the first free memory address */
        for (i = 0; i < (*cstksUsed)->length; i++) {
            if (!GGC_RAD((*cstksUsed), i)) break;
        }

        /* assign it */
        GGC_WD(node, stype, stype);
        GGC_WD(node, addr, i);
        GGC_WD(unode, stype, stype);
        GGC_WD(unode, addr, i);
        if (i >= *cstkUsed) *cstkUsed = i + 1;
        if (cstksUsed == &pstksUsed)
            GGC_WAD(pstksUsed, i, 1);
        else
            GGC_WAD(stksUsed, i, 1);

        /* and remove any that are no longer used */
        lastUsed = GGC_RP(node, lastUsed);
        if (lastUsed) {
            for (i = 0; i < lastUsed->length; i++) {
                unode = GGC_RAP(ir, GGC_RAD(lastUsed, i));
                stype = GGC_RD(unode, stype);
                addr = GGC_RD(unode, addr);
                if (stype == SDYN_STORAGE_PSTK) {
                    GGC_WAD(pstksUsed, addr, 0);
                } else if (stype == SDYN_STORAGE_STK) {
                    GGC_WAD(stksUsed, addr, 0);
                }
            }
        }
    }

    /* now go through and fix up the stack addresses, allocas and popas to account for the argument stack */
    if (astkUsed < 2) astkUsed = 2; /* always allocate some play space for pointers */
    pstkUsed += astkUsed;
    for (si = 0; si < ir->length; si++) {
        size_t addr = 0;
        node = GGC_RAP(ir, si);
        if (GGC_RD(node, stype) == SDYN_STORAGE_PSTK) {
            addr = GGC_RD(node, addr) + astkUsed;
            GGC_WD(node, addr, addr);
        }

        switch (GGC_RD(node, op)) {
            case SDYN_NODE_ALLOCA:
            case SDYN_NODE_POPA:
                GGC_WD(node, imm, stkUsed);
                break;

            case SDYN_NODE_PALLOCA:
            case SDYN_NODE_PPOPA:
                GGC_WD(node, imm, pstkUsed);
                break;
        }
    }

    return;
}

/* compile and perform register allocation */
SDyn_IRNodeArray sdyn_irCompile(SDyn_Node func, struct SDyn_RegisterMap *registerMap)
{
    SDyn_IRNodeArray ret = NULL;

    GGC_PUSH_2(func, ret);

    ret = sdyn_irCompilePrime(func);
    sdyn_irRegAlloc(ret, registerMap);

    return ret;
}

#ifdef USE_SDYN_IR_TEST
#include "sja/buffer.h"

static void dumpIR(SDyn_IRNodeArray ir)
{
    SDyn_IRNode node = NULL;
    SDyn_String string = NULL;
    SDyn_Tag tag = NULL;
    GGC_char_Array arr = NULL, yes = NULL, na = NULL;
    size_t i;
    char c;

    GGC_PUSH_7(ir, node, string, tag, arr, yes, na);

    yes = GGC_NEW_DA(char, 1);
    c = '+';
    GGC_WAD(yes, 0, c);
    na = GGC_NEW_DA(char, 1);
    c = '-';
    GGC_WAD(na, 0, c);

    for (i = 0; i < ir->length; i++) {
        node = GGC_RAP(ir, i);

        /* try to get out the immp value */
        string = (SDyn_String) GGC_RP(node, immp);
        arr = na;
        if (string) {
            arr = yes;
            tag = (SDyn_Tag) GGC_RUP(string);
            if (tag && GGC_RD(tag, type) == SDYN_TYPE_STRING)
                arr = GGC_RP(string, value);
        }

        /* and print it */
        printf("  %lu:\r\t %s\r\t\t\t t:%d\r\t\t\t\t s:%d:%lu\r\t\t\t\t\t i:%lu:%.*s\r\t\t\t\t\t\t\t o:%lu:%lu\n",
                (unsigned long) i,
                sdyn_nodeNames[GGC_RD(node, op)],
                GGC_RD(node, rtype),
                GGC_RD(node, stype), (unsigned long) GGC_RD(node, addr),
                (unsigned long) GGC_RD(node, imm), (int) arr->length, arr->a__data,
                (unsigned long) GGC_RD(node, left), (unsigned long) GGC_RD(node, right));
    }

    return;
}

int main()
{
    SDyn_Node pnode = NULL, cnode = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_IRNodeArray ir = NULL;
    size_t i;
    struct Buffer_char buf;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    sdyn_initValues();

    GGC_PUSH_4(pnode, cnode, children, ir);

    pnode = sdyn_parse(cur);
    children = GGC_RP(pnode, children);

    for (i = 0; i < children->length; i++) {
        cnode = GGC_RAP(children, i);
        if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
            printf("%.*s:\n",
                (int) GGC_RD(cnode, tok).valLen, (char *) GGC_RD(cnode, tok).val);

            ir = sdyn_irCompile(cnode, NULL);
            dumpIR(ir);
        }
    }

    return 0;
}
#endif
