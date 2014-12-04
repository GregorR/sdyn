#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc/collections/list.h"

#include "sdyn/nodes.h"
#include "sdyn/parser.h"
#include "sdyn/tokenizer.h"

/* this is as good a place as any to fill in nodeNames */
char *sdyn_nodeNames[] = {
#define SDYN_NODEX(x) #x,
#include "sdyn/nodex.h"
#undef SDYN_NODEX
    "LAST"
};

#define PEEK() (tok = *ntok)

#define NEXT() do { \
    PEEK(); \
    *ntok = sdyn_tokenize(ntok->val + ntok->valLen); \
} while(0)

#define IFTOK(ttype) if (tok.type == SDYN_TOKEN_ ## ttype)

#define IFNOTTOK(ttype) if (tok.type != SDYN_TOKEN_ ## ttype)

#define ERROR() do { \
    fprintf(stderr, "Unrecoverable error at token %.*s\n", tok.valLen, (char *) tok.val); \
    abort(); \
} while(0)

#define ASSERTTOK(ttype) IFNOTTOK(ttype) ERROR()

#define ASSERTNEXT(ttype) do { \
    NEXT(); \
    ASSERTTOK(ttype); \
} while(0)

#define RET(ttype, tokv, childrenv) do { \
    struct SDyn_Token tokvw; \
    ret = GGC_NEW(SDyn_Node); \
    GGC_WD(ret, type, SDYN_NODE_ ## ttype); \
    tokvw = (tokv); \
    GGC_WD(ret, tok, tokvw); \
    GGC_WP(ret, children, childrenv); \
} while(0)

GGC_LIST_STATIC(SDyn_Node)

#define PARSER(name) static SDyn_Node parse ## name (struct SDyn_Token *ntok)
PARSER(Top);
PARSER(FunDecl);
PARSER(VarDecl);
PARSER(GlobalCall);
PARSER(Params);
PARSER(VarDecls);
PARSER(Statements);
PARSER(Statement);
PARSER(Expression);
PARSER(ElseClause);
PARSER(LValOpt);
PARSER(OrExp);
PARSER(AndExp);
PARSER(EqExp);
PARSER(RelExp);
PARSER(AddExp);
PARSER(MulExp);
PARSER(PrefixExp);
PARSER(PostfixExp);
PARSER(Args);
PARSER(Primary);

/* the parser entry point */
SDyn_Node sdyn_parse(const unsigned char *inp)
{
    struct SDyn_Token ntok = sdyn_tokenize(inp);
    return parseTop(&ntok);
}

PARSER(Top)
{
    struct SDyn_Token tok, first;
    SDyn_Node ret = NULL, cur = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_NodeList clist = NULL;

    GGC_PUSH_4(ret, cur, children, clist);

    clist = GGC_NEW(SDyn_NodeList);
    PEEK();
    first = tok;

    while (1) {
        PEEK();

        IFTOK(function) {
            cur = parseFunDecl(ntok);
        } else IFTOK(var) {
            cur = parseVarDecl(ntok);
        } else IFTOK(ID) {
            cur = parseGlobalCall(ntok);
        } else IFTOK(EOF) {
            break;
        } else ERROR();

        SDyn_NodeListPush(clist, cur);
    }

    /* now build the return */
    children = SDyn_NodeListToArray(clist);
    RET(TOP, first, children);
    return ret;
}

PARSER(GlobalCall)
{
    SDyn_Node ret = NULL;
    struct SDyn_Token tok, id;

    GGC_PUSH_1(ret);

    ASSERTNEXT(ID);
    id = tok;
    ASSERTNEXT(LPAREN);
    ASSERTNEXT(RPAREN);
    ASSERTNEXT(SEMICOLON);

    RET(GLOBALCALL, id, GGC_NULL);
    return ret;
}

PARSER(FunDecl)
{
    SDyn_Node ret = NULL, params = NULL, varDecls = NULL, statements = NULL;
    SDyn_NodeArray children = NULL;
    struct SDyn_Token tok, id;

    GGC_PUSH_5(ret, params, varDecls, statements, children);

    ASSERTNEXT(function);
    ASSERTNEXT(ID);
    id = tok;
    ASSERTNEXT(LPAREN);
    params = parseParams(ntok);
    ASSERTNEXT(RPAREN);
    ASSERTNEXT(LBRACE);
    varDecls = parseVarDecls(ntok);
    statements = parseStatements(ntok);
    ASSERTNEXT(RBRACE);

    children = GGC_NEW_PA(SDyn_Node, 3);
    GGC_WAP(children, 0, params);
    GGC_WAP(children, 1, varDecls);
    GGC_WAP(children, 2, statements);
    RET(FUNDECL, id, children);

    return ret;
}

PARSER(VarDecls)
{
    SDyn_Node ret = NULL, cur = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_NodeList clist = NULL;
    struct SDyn_Token tok, first;

    GGC_PUSH_4(ret, cur, children, clist);

    clist = GGC_NEW(SDyn_NodeList);
    PEEK();
    first = tok;

    while (1) {
        PEEK();
        IFNOTTOK(var) break;
        cur = parseVarDecl(ntok);
        SDyn_NodeListPush(clist, cur);
    }

    children = SDyn_NodeListToArray(clist);
    RET(VARDECLS, first, children);
    return ret;
}

PARSER(VarDecl)
{
    SDyn_Node ret = NULL;
    struct SDyn_Token tok, id;

    GGC_PUSH_1(ret);

    ASSERTNEXT(var);
    ASSERTNEXT(ID);
    id = tok;
    ASSERTNEXT(SEMICOLON);

    RET(VARDECL, id, GGC_NULL);
    return ret;
}

PARSER(Params)
{
    SDyn_Node ret = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_NodeList clist = NULL;
    struct SDyn_Token tok, first;

    GGC_PUSH_3(ret, children, clist);

    clist = GGC_NEW(SDyn_NodeList);
    PEEK();
    first = tok;

    /* do we have at least one? */
    IFTOK(ID) {
        NEXT();

        /* yes. Add it */
        RET(PARAM, tok, GGC_NULL);
        SDyn_NodeListPush(clist, ret);

        /* now look for more */
        while (1) {
            PEEK();

            IFTOK(COMMA) {
                NEXT();
                ASSERTNEXT(ID);
                RET(PARAM, tok, GGC_NULL);
                SDyn_NodeListPush(clist, ret);
            } else break;
        }
    }

    /* and prepare the return */
    children = SDyn_NodeListToArray(clist);
    RET(PARAMS, first, children);
    return ret;
}

PARSER(Statements)
{
    SDyn_Node ret = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_NodeList clist = NULL;
    struct SDyn_Token tok, first;

    GGC_PUSH_3(ret, children, clist);

    clist = GGC_NEW(SDyn_NodeList);
    PEEK();
    first = tok;

    while (1) {
        /* the only token that cannot be a statement is } */
        PEEK();
        IFTOK(RBRACE) break;
        ret = parseStatement(ntok);
        SDyn_NodeListPush(clist, ret);
    }

    children = SDyn_NodeListToArray(clist);
    RET(STATEMENTS, first, children);
    return ret;
}

PARSER(Statement)
{
    SDyn_Node ret = NULL;
    SDyn_NodeArray children = NULL;
    struct SDyn_Token tok, rep;

    GGC_PUSH_2(ret, children);

    PEEK();

    IFTOK(if) {
        NEXT();

        /* if statement */
        rep = tok;
        children = GGC_NEW_PA(SDyn_Node, 3);
        ASSERTNEXT(LPAREN);
        ret = parseExpression(ntok);
        GGC_WAP(children, 0, ret);
        ASSERTNEXT(RPAREN);
        ASSERTNEXT(LBRACE);
        ret = parseStatements(ntok);
        GGC_WAP(children, 1, ret);
        ret = parseElseClause(ntok);
        GGC_WAP(children, 2, ret);
        RET(IF, rep, children);
        return ret;

    } else IFTOK(while) {
        NEXT();

        /* while statement */
        rep = tok;
        children = GGC_NEW_PA(SDyn_Node, 2);
        ASSERTNEXT(LPAREN);
        ret = parseExpression(ntok);
        GGC_WAP(children, 0, ret);
        ASSERTNEXT(RPAREN);
        ASSERTNEXT(LBRACE);
        ret = parseStatements(ntok);
        GGC_WAP(children, 1, ret);
        ASSERTNEXT(RBRACE);
        RET(WHILE, rep, children);
        return ret;

    } else IFTOK(return) {
        NEXT();

        /* return statement */
        rep = tok;
        children = GGC_NEW_PA(SDyn_Node, 1);
        ret = parseExpression(ntok);
        GGC_WAP(children, 0, ret);
        ASSERTNEXT(SEMICOLON);
        RET(RETURN, rep, children);
        return ret;

    } else {
        /* expression statement */
        ret = parseExpression(ntok);
        ASSERTNEXT(SEMICOLON);
        return ret;

    }
}

PARSER(ElseClause)
{
    SDyn_Node ret = NULL;
    struct SDyn_Token tok;

    GGC_PUSH_1(ret);

    PEEK();
    IFTOK(else) {
        NEXT();
        ASSERTNEXT(LBRACE);
        ret = parseStatements(ntok);
        ASSERTNEXT(RBRACE);
    }

    return ret;
}

PARSER(Expression)
{
    SDyn_Node ret = NULL, left = NULL, right = NULL;
    SDyn_NodeArray children = NULL;
    struct SDyn_Token tok, rep;

    GGC_PUSH_4(ret, left, right, children);

    left = parseLValOpt(ntok);
    if (left) {
        /* the left appeared to be a valid lvalue, is this an assignment? */
        PEEK();
        IFTOK(ASSIGN) {
            /* it is */
            NEXT();
            rep = tok;
            right = parseExpression(ntok);
            children = GGC_NEW_PA(SDyn_Node, 2);
            GGC_WAP(children, 0, left);
            GGC_WAP(children, 1, right);
            RET(ASSIGN, rep, children);
            return ret;

        } else {
            /* no, just the lvalue */
            return left;

        }

    } else {
        /* no, must be some other kind of expression */
        return parseOrExp(ntok);

    }
}

#define BINARY_HEAD(name, sub) \
PARSER(name) { \
    SDyn_Node ret = NULL, right = NULL; \
    SDyn_NodeArray children = NULL; \
    struct SDyn_Token tok, rep; \
    GGC_PUSH_3(ret, right, children); \
    ret = parse ## sub(ntok); \
    while (1) { \
        PEEK();

#define BINARY(ttype, ntype, sub) \
    IFTOK(ttype) { \
        NEXT(); \
        rep = tok; \
        right = parse ## sub(ntok); \
        children = GGC_NEW_PA(SDyn_Node, 2); \
        GGC_WAP(children, 0, ret); \
        GGC_WAP(children, 1, right); \
        RET(ntype, rep, children); \
    } else

#define BINARY_TAIL \
        break; \
    } \
    return ret; \
}

BINARY_HEAD(OrExp, AndExp)
    BINARY(OR, OR, AndExp)
BINARY_TAIL

BINARY_HEAD(AndExp, EqExp)
    BINARY(AND, AND, EqExp)
BINARY_TAIL

BINARY_HEAD(EqExp, RelExp)
    BINARY(EQ, EQ, RelExp)
    BINARY(NE, NE, RelExp)
BINARY_TAIL

BINARY_HEAD(RelExp, AddExp)
    BINARY(LT, LT, AddExp)
    BINARY(GT, GT, AddExp)
    BINARY(LE, LE, AddExp)
    BINARY(LE, LE, AddExp)
BINARY_TAIL

BINARY_HEAD(AddExp, MulExp)
    BINARY(ADD, ADD, MulExp)
    BINARY(SUB, SUB, MulExp)
BINARY_TAIL

BINARY_HEAD(MulExp, PrefixExp)
    BINARY(MUL, MUL, PrefixExp)
    BINARY(MOD, MOD, PrefixExp)
BINARY_TAIL

PARSER(PrefixExp)
{
    SDyn_Node ret = NULL, left = NULL, right = NULL;
    SDyn_NodeArray children = NULL;
    struct SDyn_Token tok, rep;

    GGC_PUSH_4(ret, left, right, children);

    PEEK();
    IFTOK(BNOT) {
        NEXT();

        /* ~ ~ ( MulExp / PrefixExp ) */
        ASSERTNEXT(BNOT);
        ASSERTNEXT(LPAREN);
        left = parseMulExp(ntok);
        ASSERTNEXT(DIV);
        rep = tok;
        right = parsePrefixExp(ntok);
        ASSERTNEXT(RPAREN);

        children = GGC_NEW_PA(SDyn_Node, 2);
        GGC_WAP(children, 0, left);
        GGC_WAP(children, 1, right);

        RET(DIV, rep, children);
        return ret;

    } else IFTOK(NOT) {
        NEXT();

        /* ! PrefixExp */
        rep = tok;
        ret = parsePrefixExp(ntok);
        children = GGC_NEW_PA(SDyn_Node, 1);
        GGC_WAP(children, 0, ret);
        RET(NOT, rep, children);
        return ret;

    } else IFTOK(typeof) {
        NEXT();

        /* typeof PrefixExp */
        rep = tok;
        ret = parsePrefixExp(ntok);
        children = GGC_NEW_PA(SDyn_Node, 1);
        GGC_WAP(children, 0, ret);
        RET(TYPEOF, rep, children);
        return ret;

    }

    return parsePostfixExp(ntok);
}

PARSER(PostfixExp)
{
    SDyn_Node ret = NULL, right = NULL;
    SDyn_NodeArray children = NULL;
    struct SDyn_Token tok, id, rep;

    GGC_PUSH_3(ret, right, children);

    PEEK();

    /* only special case is an intrinsic call */
    IFTOK(INTRINSIC) {
        NEXT();
        id = tok;

        /* <intrinsic> ( Args ) */
        ASSERTNEXT(LPAREN);
        ret = parseArgs(ntok);
        ASSERTNEXT(RPAREN);
        children = GGC_NEW_PA(SDyn_Node, 1);
        GGC_WAP(children, 0, ret);
        RET(INTRINSICCALL, id, children);

    } else {
        ret = parsePrimary(ntok);

    }

    /* from there, look for postfix */
    while (1) {
        PEEK();

        IFTOK(LPAREN) {
            NEXT();

            /* PostfixExp ( Args ) */
            rep = tok;
            right = parseArgs(ntok);
            ASSERTNEXT(RPAREN);
            children = GGC_NEW_PA(SDyn_Node, 2);
            GGC_WAP(children, 0, ret);
            GGC_WAP(children, 1, right);
            RET(CALL, rep, children);

        } else IFTOK(LBRACKET) {
            NEXT();

            /* PostfixExp [ Expression ] */
            rep = tok;
            right = parseExpression(ntok);
            ASSERTNEXT(RBRACKET);
            children = GGC_NEW_PA(SDyn_Node, 2);
            GGC_WAP(children, 0, ret);
            GGC_WAP(children, 1, right);
            RET(INDEX, rep, children);

        } else IFTOK(DOT) {
            NEXT();

            /* PostfixExp . <id> */
            ASSERTNEXT(ID);
            id = tok;
            children = GGC_NEW_PA(SDyn_Node, 1);
            GGC_WAP(children, 0, ret);
            RET(MEMBER, id, children);

        } else break;
    }

    return ret;
}

/* LVals are really just a special case of OrExps, grammatically */
PARSER(LValOpt)
{
    SDyn_Node ret = NULL;
    struct SDyn_Token tok, start;
    int type;

    GGC_PUSH_1(ret);

    /* make sure we can rewind */
    start = *ntok;

    /* parse it */
    ret = parseOrExp(ntok);

    /* check if it's a valid for */
    type = GGC_RD(ret, type);
    if (type == SDYN_NODE_INDEX ||
        type == SDYN_NODE_MEMBER ||
        type == SDYN_NODE_VARREF)
        return ret;

    /* otherwise, discard */
    *ntok = start;
    return NULL;
}

PARSER(Args)
{
    SDyn_Node ret = NULL, cur = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_NodeList clist = NULL;
    struct SDyn_Token tok, first;

    GGC_PUSH_4(ret, cur, children, clist);

    /* if it's just a ), no args at all */
    PEEK();
    first = tok;
    IFTOK(RPAREN) {
        children = GGC_NEW_PA(SDyn_Node, 0);
        RET(ARGS, first, children);
        return ret;
    }

    /* otherwise, we'd best have a list of args! */
    clist = GGC_NEW(SDyn_NodeList);
    cur = parseExpression(ntok);
    SDyn_NodeListPush(clist, cur);
    while (1) {
        PEEK();

        IFTOK(COMMA) {
            NEXT();

            cur = parseExpression(ntok);
            SDyn_NodeListPush(clist, cur);
        } else break;
    }

    children = SDyn_NodeListToArray(clist);
    RET(ARGS, first, children);

    return ret;
}

PARSER(Primary)
{
    SDyn_Node ret = NULL;
    struct SDyn_Token tok, rep;

    GGC_PUSH_1(ret);

    NEXT();

    IFTOK(ID) {
        RET(VARREF, tok, GGC_NULL);
        return ret;
    } else IFTOK(NUM) {
        RET(NUM, tok, GGC_NULL);
        return ret;
    } else IFTOK(STR) {
        RET(STR, tok, GGC_NULL);
        return ret;
    } else IFTOK(false) {
        RET(FALSE, tok, GGC_NULL);
        return ret;
    } else IFTOK(true) {
        RET(TRUE, tok, GGC_NULL);
        return ret;
    } else IFTOK(LBRACE) {
        rep = tok;
        ASSERTNEXT(RBRACE);
        RET(OBJ, rep, GGC_NULL);
        return ret;
    } else IFTOK(LPAREN) {
        ret = parseExpression(ntok);
        ASSERTNEXT(RPAREN);
        return ret;
    } else ERROR();
}

#ifdef USE_SDYN_PARSER_TEST
#include "buffer.h"

static void dumpNode(size_t spcs, SDyn_Node node)
{
    SDyn_NodeArray children = NULL;
    size_t i;

    GGC_PUSH_2(node, children);

    for (i = 0; i < spcs; i++) printf("  ");
    printf("%s: %.*s\n", sdyn_nodeNames[GGC_RD(node, type)], GGC_RD(node, tok).valLen, (char *) GGC_RD(node, tok).val);

    spcs++;
    children = GGC_RP(node, children);
    if (children) {
        for (i = 0; i < children->length; i++) {
            dumpNode(spcs, GGC_RAP(children, i));
        }
    }

    return;
}

int main()
{
    SDyn_Node node = NULL;
    struct Buffer_char buf;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    GGC_PUSH_1(node);

    node = sdyn_parse(cur);
    dumpNode(0, node);

    return 0;
}
#endif
