/*
 * SDyn tokenizer
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

#include <string.h>

#include "sdyn/tokenizer.h"

static int isWhite(unsigned char c)
{
    return ((c == ' ') ||
            (c == '\t') ||
            (c == '\n') ||
            (c == '\r'));
}

/* get a single token from the input */
struct SDyn_Token sdyn_tokenize(const unsigned char *inp)
{
    struct SDyn_Token ret;
    memset(&ret, 0, sizeof(ret));

    /* skip whitespace and comments */
    while (1) {
        while (isWhite(*inp)) inp++;
        if (inp[0] == '/') {
            if (inp[1] == '/') {
                /* line comment */
                inp += 2;
                while (*inp && *inp != '\n') inp++;
            } else if (inp[1] == '*') {
                /* block comment */
                inp += 2;
                while (*inp && (inp[0] != '*' || inp[1] != '/')) inp++;
                if (*inp) inp += 2;
            } else break;
        } else break;
    }

    /* if there's no more input, so be it */
    if (!*inp) {
        ret.type = SDYN_TOKEN_EOF;
        return ret;
    }
    ret.val = inp;

    /* is it an identifier or intrinsic? */
    if ((*inp >= 'a' && *inp <= 'z') ||
        (*inp >= 'A' && *inp <= 'Z') ||
        (*inp == '$')) {
        size_t len;

        /* yes. Get the rest */
        for (len = 1;
             ((inp[len] >= 'a' && inp[len] <= 'z') ||
              (inp[len] >= 'A' && inp[len] <= 'Z') ||
              (inp[len] >= '0' && inp[len] <= '9'));
             len++);

        /* set up the return */
        ret.type = (*inp == '$') ? SDYN_TOKEN_INTRINSIC : SDYN_TOKEN_ID;
        ret.valLen = len;

        /* and special-case our keywords */
#define KEY(k) \
        if (sizeof(#k)-1 == len && !strncmp(#k, inp, len)) \
            ret.type = SDYN_TOKEN_ ## k
        KEY(else);
        else KEY(false);
        else KEY(function);
        else KEY(if);
        else KEY(null);
        else KEY(return);
        else KEY(true);
        else KEY(typeof);
        else KEY(var);
        else KEY(while);
#undef KEY

        return ret;
    }

    /* is it a number? */
    if (*inp >= '0' && *inp <= '9') {
        size_t len;

        /* yes. Get the rest */
        for (len = 1;
             inp[len] >= '0' && inp[len] <= '9';
             len++);

        /* set up the return */
        ret.type = SDYN_TOKEN_NUM;
        ret.valLen = len;
        return ret;
    }

    /* is it a string literal? */
    if (*inp == '"') {
        size_t len;

        /* yes. Get the rest */
        for (len = 1; inp[len] && inp[len] != '"'; len++) {
            if (inp[len] == '\\') {
                if (inp[len+1])
                    len++;
            }
        }
        if (inp[len] == '"') len++;

        /* set up the return */
        ret.type = SDYN_TOKEN_STR;
        ret.valLen = len;
        return ret;
    }

    /* all the simple one-character symbols */
#define SYM(s, ch) \
    if (*inp == ch) do { \
        ret.type = SDYN_TOKEN_ ## s; \
        ret.valLen = 1; \
        return ret; \
    } while(0)
    SYM(LPAREN, '(');
    else SYM(RPAREN, ')');
    else SYM(LBRACE, '{');
    else SYM(RBRACE, '}');
    else SYM(LBRACKET, '[');
    else SYM(RBRACKET, ']');
    else SYM(SEMICOLON, ';');
    else SYM(COMMA, ',');
    else SYM(ADD, '+');
    else SYM(SUB, '-');
    else SYM(MUL, '*');
    else SYM(MOD, '%');
    else SYM(DIV, '/');
    else SYM(BNOT, '~');
    else SYM(DOT, '.');
#undef SYM

    /* and now for all the two-character symbols */
#define SYM2(s, ch) if (inp[1] == ch) do { \
    ret.type = SDYN_TOKEN_ ## s; \
    ret.valLen = 2; \
    return ret; \
} while(0)
#define SYM1(s) do { \
    ret.type = SDYN_TOKEN_ ## s; \
    ret.valLen = 1; \
    return ret; \
} while(0)
    switch (*inp) {
        case '|':
            SYM2(OR, '|');
            break;

        case '&':
            SYM2(AND, '&');
            break;

        case '=':
            SYM2(EQ, '=');
            else SYM1(ASSIGN);

        case '!':
            SYM2(NE, '=');
            else SYM1(NOT);

        case '<':
            SYM2(LE, '=');
            else SYM1(LT);

        case '>':
            SYM2(GE, '=');
            else SYM1(GT);
    }
#undef SYM1
#undef SYM2

    /* all else failed! */
    ret.type = SDYN_TOKEN_ERR;
    ret.valLen = 1;
    return ret;
}

#ifdef USE_SDYN_TOKENIZER_TEST
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "sja/buffer.h"

int main()
{
    struct Buffer_char buf;
    struct SDyn_Token tok;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    while (1) {
        tok = sdyn_tokenize(cur);
        if (tok.type == SDYN_TOKEN_EOF) break;
        cur = tok.val + tok.valLen;

        printf("Token %d: %.*s\n", tok.type, (int) tok.valLen, (char *) tok.val);
    }

    return 0;
}
#endif
