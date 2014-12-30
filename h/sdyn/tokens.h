#ifndef SDYN_TOKENS_H
#define SDYN_TOKENS_H 1

enum SDyn_TokenType {
    SDYN_TOKEN_ERR,
    SDYN_TOKEN_EOF,

    /* non-finite */
    SDYN_TOKEN_ID,
    SDYN_TOKEN_NUM,
    SDYN_TOKEN_STR,
    SDYN_TOKEN_INTRINSIC,

    /* symbols */
    SDYN_TOKEN_LPAREN, /* ( */
    SDYN_TOKEN_RPAREN, /* ) */
    SDYN_TOKEN_LBRACE, /* { */
    SDYN_TOKEN_RBRACE, /* } */
    SDYN_TOKEN_LBRACKET, /* [ */
    SDYN_TOKEN_RBRACKET, /* ] */
    SDYN_TOKEN_SEMICOLON,
    SDYN_TOKEN_COMMA,
    SDYN_TOKEN_ASSIGN,
    SDYN_TOKEN_OR,
    SDYN_TOKEN_AND,
    SDYN_TOKEN_EQ,
    SDYN_TOKEN_NE,
    SDYN_TOKEN_LT,
    SDYN_TOKEN_GT,
    SDYN_TOKEN_LE,
    SDYN_TOKEN_GE,
    SDYN_TOKEN_ADD,
    SDYN_TOKEN_SUB,
    SDYN_TOKEN_MUL,
    SDYN_TOKEN_MOD,
    SDYN_TOKEN_DIV,
    SDYN_TOKEN_BNOT,
    SDYN_TOKEN_NOT,
    SDYN_TOKEN_DOT,

    /* keywords */
    SDYN_TOKEN_else,
    SDYN_TOKEN_false,
    SDYN_TOKEN_function,
    SDYN_TOKEN_if,
    SDYN_TOKEN_null,
    SDYN_TOKEN_return,
    SDYN_TOKEN_true,
    SDYN_TOKEN_typeof,
    SDYN_TOKEN_var,
    SDYN_TOKEN_while,

    SDYN_TOKEN_LAST
};

#endif
