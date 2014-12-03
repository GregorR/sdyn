#ifndef SDYN_NODES_H
#define SDYN_NODES_H 1

enum SDyn_NodeType {
    SDYN_NODE_NIL,

    SDYN_NODE_TOP, /* list */
    SDYN_NODE_GLOBALCALL, /* id [] */
    SDYN_NODE_FUNDECL, /* id [Params, VarDecls, Statements] */
    SDYN_NODE_VARDECLS, /* list */
    SDYN_NODE_VARDECL, /* id [] */
    SDYN_NODE_PARAMS, /* list */
    SDYN_NODE_PARAM, /* id [] */
    SDYN_NODE_STATEMENTS, /* list */
    SDYN_NODE_IF, /* - [Expression, Statements, ElseClause] */
    SDYN_NODE_WHILE, /* - [Expression, Statements] */
    SDYN_NODE_RETURN, /* - [Expression] */
    SDYN_NODE_ASSIGN, /* binary... */
    SDYN_NODE_OR,
    SDYN_NODE_AND,
    SDYN_NODE_EQ,
    SDYN_NODE_NE,
    SDYN_NODE_LT,
    SDYN_NODE_GT,
    SDYN_NODE_LE,
    SDYN_NODE_GE,
    SDYN_NODE_ADD,
    SDYN_NODE_SUB,
    SDYN_NODE_MUL,
    SDYN_NODE_MOD,
    SDYN_NODE_DIV, /* ... /binary */
    SDYN_NODE_NOT, /* - [PrefixExp] */
    SDYN_NODE_TYPEOF, /* - [PrefixExp] */
    SDYN_NODE_CALL, /* - [PostfixExp, Args] */
    SDYN_NODE_INDEX, /* - [PostfixExp, Expression] */
    SDYN_NODE_MEMBER, /* id [PostfixExp] */
    SDYN_NODE_INTRINSICCALL, /* intrinsic [Args] */
    SDYN_NODE_ARGS, /* list */
    SDYN_NODE_VARREF, /* id [] */
    SDYN_NODE_NUM, /* num [] */
    SDYN_NODE_STR, /* str [] */
    SDYN_NODE_FALSE, /* - [] */
    SDYN_NODE_TRUE, /* - [] */
    SDYN_NODE_OBJ, /* - [] */

    /* nodes for IR only */
    SDYN_NODE_ALLOCA, /* allocate space on the stack */
    SDYN_NODE_POPA, /* free space from the stack */

    SDYN_NODE_LAST
};

#endif
