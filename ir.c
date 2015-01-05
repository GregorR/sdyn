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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc/collections/list.h"

#include "sdyn/ir.h"
#include "sdyn/value.h"

GGC_LIST_STATIC(SDyn_IRNode)

/* compile a parse tree node to IR */
static size_t irCompileNode(SDyn_IRNodeList ir, SDyn_Node node, SDyn_IndexMap symbols)
{
    SDyn_NodeArray children = NULL;
    SDyn_Node cnode = NULL;
    SDyn_IRNode irn = NULL;
    SDyn_String name = NULL;
    GGC_size_t_Unit indexBox = NULL, indexBox2 = NULL;
    SDyn_IndexMap symbols2 = NULL;

    struct SDyn_Token tok;
    size_t i;

    GGC_PUSH_10(ir, node, symbols, children, cnode, irn, name, indexBox, indexBox2, symbols2);

    children = GGC_RP(node, children);

#define SUB(x) irCompileNode(ir, GGC_RAP(children, x), symbols)
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
            SDyn_IRNodeListPush(ir, irn);
            irn = GGC_NEW(SDyn_IRNode);
            GGC_WD(irn, op, SDYN_NODE_RETURN);
            i = GGC_RD(ir, length) - 1;
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
            /* simple list */
            for (i = 0; i < children->length; i++) {
                size_t paramIdx;

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
                GGC_WD(irn, imm, i);

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

                    /* make the node */
                    IRNNEW();
                    GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                    GGC_WD(irn, left, val);
                    val = GGC_RD(ir, length);

                    /* update the symbol table */
                    tok = GGC_RD(cnode, tok);
                    name = sdyn_boxString(NULL, (char *) tok.val, tok.valLen);
                    indexBox = GGC_NEW(GGC_size_t_Unit);
                    GGC_WD(indexBox, v, val);
                    SDyn_IndexMapPut(symbols, name, indexBox);

                    SDyn_IRNodeListPush(ir, irn);

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

        case SDYN_NODE_WHILE:
        {
            size_t begin, cond;

            /* mark the beginning */
            IRNNEW();
            begin = GGC_RD(ir, length);
            SDyn_IRNodeListPush(ir, irn);

            /* we'll need to compare our symbol table before and after to unify, so first, copy */
            symbols2 = GGC_NEW(SDyn_IndexMap);
            for (i = 0; i < GGC_RD(symbols, size); i++) {
                name = GGC_RAP(GGC_RP(symbols, keys), i);
                if (name) {
                    indexBox = GGC_RAP(GGC_RP(symbols, values), i);
                    SDyn_IndexMapPut(symbols2, name, indexBox);
                }
            }

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
            for (i = 0; i < GGC_RD(symbols2, size); i++) {
                name = GGC_RAP(GGC_RP(symbols2, keys), i);
                if (name) {
                    size_t idx;
                    indexBox2 = GGC_RAP(GGC_RP(symbols2, values), i);
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

                    /* NOP it so the while loop keeps it alive */
                    irn = GGC_NEW(SDyn_IRNode);
                    GGC_WD(irn, op, SDYN_NODE_NOP);
                    GGC_WD(irn, left, idx);
                    SDyn_IRNodeListPush(ir, irn);
                }
            }
            break;
        }

        case SDYN_NODE_MEMBER:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);

            /* get the object */
            i = SUB(0);
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
            GGC_WD(irn, left, i);

            /* and the index */
            i = SUB(1);
            GGC_WD(irn, right, i);

            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_CALL:
        {
            size_t f;

            /* get the function to call */
            f = SUB(0);

            /* get all the arguments */
            cnode = GGC_RAP(children, 1);
            children = GGC_RP(cnode, children);
            for (i = 0; i < children->length; i++) {
                size_t v;

                /* put in an arg slot */
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_ARG);
                GGC_WD(irn, imm, i);
                v = SUB(i);
                GGC_WD(irn, left, v);
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
            /* get all the arguments */
            cnode = GGC_RAP(children, 0);
            children = GGC_RP(cnode, children);
            for (i = 0; i < children->length; i++) {
                size_t v;

                /* put in an arg slot */
                irn = GGC_NEW(SDyn_IRNode);
                GGC_WD(irn, op, SDYN_NODE_ARG);
                GGC_WD(irn, imm, i);
                v = SUB(i);
                GGC_WD(irn, left, v);
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

        /* binary nodes: */
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
    irCompileNode(ir, func, symbols);

    /* convert to array */
    ret = SDyn_IRNodeListToArray(ir);

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

    /* first off, default uidxs */
    irUsed = GGC_NEW_DA(char, ir->length);
    for (si = ir->length - 1; si >= 0; si--) {
        li = 0;
        node = GGC_RAP(ir, si);
        GGC_WD(node, uidx, si);
    }

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
            case SDYN_NODE_UNIFY:
                /* set the uidx on both unified nodes, and force them to box in case their types differ */
                idx = GGC_RD(node, uidx);
                GGC_WD(node, rtype, SDYN_TYPE_BOXED);
                unode = GGC_RAP(ir, GGC_RD(node, left));
                GGC_WD(unode, uidx, idx);
                GGC_WD(unode, rtype, SDYN_TYPE_BOXED);
                unode = GGC_RAP(ir, GGC_RD(node, right));
                GGC_WD(unode, uidx, idx);
                GGC_WD(unode, rtype, SDYN_TYPE_BOXED);
                break;

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
        if (GGC_RD(node, rtype) >= SDYN_TYPE_FIRST_BOXED) {
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
