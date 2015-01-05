/*
 * SDyn parser
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
