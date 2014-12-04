#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc/collections/list.h"

#include "sdyn/ir.h"
#include "sdyn/value.h"

GGC_LIST_STATIC(SDyn_IRNode)

/* compile a node to IR */
static size_t irCompileNode(SDyn_IRNodeList ir, SDyn_Node node, SDyn_IndexMap symbols)
{
    SDyn_NodeArray children = NULL;
    SDyn_Node cnode = NULL;
    SDyn_IRNode irn = NULL;
    SDyn_String name = NULL;
    GGC_size_t_Unit indexBox = NULL;

    struct SDyn_Token tok;
    size_t i;

    GGC_PUSH_8(ir, node, symbols, children, cnode, irn, name, indexBox);

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
            GGC_WD(irn, op, SDYN_NODE_POPA);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_PARAMS:
            /* simple list */
            for (i = 0; i < children->length; i++) {
                cnode = GGC_RAP(children, i);

                tok = GGC_RD(cnode, tok);
                name = sdyn_boxString((char *) tok.val, tok.valLen);

                /* add it to the symbol table */
                indexBox = GGC_NEW(GGC_size_t_Unit);
                i = GGC_RD(ir, length);
                GGC_WD(indexBox, v, i);
                SDyn_IndexMapPut(symbols, name, indexBox);

                /* make the IR node */
                IRNNEW();
                GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
                GGC_WD(irn, imm, i);

                /* add it to the list */
                SDyn_IRNodeListPush(ir, irn);
            }
            break;

        case SDYN_NODE_VARDECL:
            tok = GGC_RD(node, tok);
            name = sdyn_boxString((char *) tok.val, tok.valLen);

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
                    name = sdyn_boxString((char *) tok.val, tok.valLen);
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

                    /* update the symbol table */
                    tok = GGC_RD(cnode, tok);
                    name = sdyn_boxString((char *) tok.val, tok.valLen);
                    indexBox = GGC_NEW(GGC_size_t_Unit);
                    GGC_WD(indexBox, v, val);
                    SDyn_IndexMapPut(symbols, name, indexBox);

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
            name = sdyn_boxString((char *) tok.val, tok.valLen);
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
            name = sdyn_boxString((char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name);
            SDyn_IRNodeListPush(ir, irn);

            break;
        }

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
            name = sdyn_boxString((char *) tok.val, tok.valLen);
            GGC_WP(irn, immp, name);
            SDyn_IRNodeListPush(ir, irn);

            break;
        }

        /* 0-ary */
        case SDYN_NODE_OBJ:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_OBJECT);
            SDyn_IRNodeListPush(ir, irn);
            break;

        case SDYN_NODE_NUM:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_INT);
            tok = GGC_RD(node, tok);
            name = sdyn_boxString((char *) tok.val, tok.valLen);
            {
                long v = sdyn_toNumber((SDyn_Undefined) name);
                GGC_WD(irn, imm, v);
            }
            SDyn_IRNodeListPush(ir, irn);
            break;

        /* unary */
        case SDYN_NODE_RETURN:
            IRNNEW();
            i = SUB(0);
            GGC_WD(irn, left, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        /* binary */
        case SDYN_NODE_ADD:
            IRNNEW();
            GGC_WD(irn, rtype, SDYN_TYPE_BOXED);
            i = SUB(0);
            GGC_WD(irn, left, i);
            i = SUB(1);
            GGC_WD(irn, right, i);
            SDyn_IRNodeListPush(ir, irn);
            break;

        default:
            fprintf(stderr, "Unsupported node %s! (%.*s)\n",
                sdyn_nodeNames[GGC_RD(node, type)], GGC_RD(node, tok).valLen, GGC_RD(node, tok).val);
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
    /* FIXME */
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
#include "buffer.h"

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
        printf("%lu:\r\t %s\r\t\t\t t:%d\r\t\t\t\t s:%d:%lu\r\t\t\t\t\t i:%lu:%.*s\r\t\t\t\t\t\t\t o:%lu:%lu\n",
                (unsigned long) i,
                sdyn_nodeNames[GGC_RD(node, op)],
                GGC_RD(node, rtype),
                GGC_RD(node, stype), (unsigned long) GGC_RD(node, addr),
                (unsigned long) GGC_RD(node, imm), arr->length, arr->a__data,
                (unsigned long) GGC_RD(node, left), (unsigned long) GGC_RD(node, right));
    }

    return;
}

int main()
{
    SDyn_Node pnode = NULL;
    SDyn_IRNodeArray ir = NULL;
    struct Buffer_char buf;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    sdyn_initValues();

    GGC_PUSH_2(pnode, ir);

    pnode = sdyn_parse(cur);
    ir = sdyn_irCompile(pnode, NULL);
    dumpIR(ir);

    return 0;
}
#endif
