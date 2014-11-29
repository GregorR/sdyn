#ifndef SDYN_TOKENIZER_H
#define SDYN_TOKENIZER_H 1

#include "tokens.h"

struct SDyn_Token {
    enum SDyn_TokenType type;
    size_t valLen;
    const unsigned char *val;
};

/* get a single token */
struct SDyn_Token sdyn_tokenize(const unsigned char *inp);

#endif
