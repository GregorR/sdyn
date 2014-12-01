#ifndef SDYN_PARSER_H
#define SDYN_PARSER_H 1

#include "ggggc/gc.h"

#include "tokenizer.h"

/* nodes are simply a type, optional token, and children */
GGC_TYPE(SDyn_Node)
    GGC_MDATA(int, type);
    GGC_MDATA(struct SDyn_Token, tok);
    GGC_MPTR(SDyn_NodeArray, children);
GGC_END_TYPE(SDyn_Node,
    GGC_PTR(SDyn_Node, children)
    );

/* the parser entry point */
SDyn_Node sdyn_parse(const unsigned char *inp);

#endif
