/* NODE MACRO               PARSER INFO             IR INFO */
/*                          Format:
 *                          <corresponding token or -> [<children>]
 *                          OR
 *                          list (for simple lists)
 *                                                  Format:
 *                                                  i:immediate number
 *                                                  s:immediate string
 *                                                  l:left operand
 *                                                  r:right operand
 *                                                  3:third operand */
SDYN_NODEX(NIL)         /*  - []                    eval to undefined */
SDYN_NODEX(TOP)         /*  list                    global object   */
SDYN_NODEX(GLOBALCALL)  /*  <id> []                 unused          */
SDYN_NODEX(FUNDECL)     /*  <id> [Params, VarDecls, Statements]
                                                    unused          */
SDYN_NODEX(VARDECLS)    /*  list                    unused          */
SDYN_NODEX(VARDECL)     /*  <id> []                 unused          */
SDYN_NODEX(PARAMS)      /*  list                    unused          */
SDYN_NODEX(PARAM)       /*  <id> []                 i:parameter number */
SDYN_NODEX(STATEMENTS)  /*  list                    unused          */
SDYN_NODEX(IF)          /*  - [Expression, Statements, ElseClause]
                                                    TBD             */
SDYN_NODEX(WHILE)       /*  - [Expression, Statements]
                                                    just marks the beginning of
                                                    a loop */
SDYN_NODEX(RETURN)      /*  - [Expression]          l:Expression    */
SDYN_NODEX(ASSIGN)      /*  - [LVal, Expression]    unused          */

/* BINARY EXPRESSIONS:      - [Left expression, Right expression]
 *                                                  l:Left expression
 *                                                  r:Right expression */
SDYN_NODEX(OR)
SDYN_NODEX(AND)
SDYN_NODEX(EQ)
SDYN_NODEX(NE)
SDYN_NODEX(LT)
SDYN_NODEX(GT)
SDYN_NODEX(LE)
SDYN_NODEX(GE)
SDYN_NODEX(ADD)
SDYN_NODEX(SUB)
SDYN_NODEX(MUL)
SDYN_NODEX(MOD)
SDYN_NODEX(DIV)
/* /BINARY */

/* UNARY EXPRESSIONS:       - [Expression]          l:Expression    */
SDYN_NODEX(NOT)
SDYN_NODEX(TYPEOF)
/* /UNARY */

SDYN_NODEX(CALL)        /*  - [PostfixExpression, Args]
                                                    Args implicit
                                                    l:PostfixExpression */
SDYN_NODEX(INDEX)       /*  - [PostfixExpression, Expression]
                                                    l:PostfixExpression
                                                    r:Expression        */
SDYN_NODEX(MEMBER)      /* <id> [PostfixExpression]
                                                    s:<id>
                                                    l:PostfixExpression */
SDYN_NODEX(INTRINSICCALL)
                        /* <id> [Args]              Args implicit
                         *                          s:<id>          */
SDYN_NODEX(ARGS)        /*  list                    unused          */
SDYN_NODEX(VARREF)      /*  <id> []                 unused          */
SDYN_NODEX(NUM)         /*  <num> []                i:<num>         */
SDYN_NODEX(STR)         /*  <str> []                s:resolved string value */
SDYN_NODEX(FALSE)       /*  - []                    eval to false   */
SDYN_NODEX(TRUE)        /*  - []                    eval to true    */
SDYN_NODEX(OBJ)         /*  - []                    eval to new object */

/* nodes for IR only */
SDYN_NODEX(NOP)             /* do nothing with a value
                               l:value */
SDYN_NODEX(UNIFY)           /* unify two values
                               l:value 1
                               r:value 2 */

/* with while loops, WHILE is the start, WCOND is the condition, and WEND is the end */
SDYN_NODEX(WCOND)
SDYN_NODEX(WEND)

SDYN_NODEX(ALLOCA)          /* allocates space. Always the first instruction.
                               Updated by register allocation to be able to
                               spill. i:Number of words to reserve */
SDYN_NODEX(PALLOCA)         /* allocate pointer space. Always the second
                               instruction. Updated by register allocation.
                               i:Number of words to reserve */
SDYN_NODEX(POPA)            /* pops space allocated by ALLOCA. Always the last
                               instruction */
SDYN_NODEX(PPOPA)           /* pops space allocated by PALLOCA. Always the
                               second-to-last instruction */

SDYN_NODEX(ASSIGNINDEX)     /* x[y]=z
                               l:x
                               r:y
                               3:z */
SDYN_NODEX(ASSIGNMEMBER)    /* x.y=z
                               s:y
                               l:x
                               r:z */

SDYN_NODEX(ARG)             /* used implicitly by *CALL
                               i:argument number
                               l:value */
