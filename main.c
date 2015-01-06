/*
 * SDyn main entry point
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

#include "arg.h"

#include "sja/buffer.h"

#include "sdyn/jit.h"

int main(int argc, char **argv)
{
    SDyn_Node pnode = NULL, cnode = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_String name = NULL;
    SDyn_Function func = NULL;
    size_t i;
    struct Buffer_char buf;
    FILE *f;
    const unsigned char *cur;
    int hadFile = 0;
    ARG_VARS;

    sdyn_initValues();

    GGC_PUSH_5(pnode, cnode, children, name, func);

    ARG_NEXT();
    while (argType) {
        if (argType == ARG_VAL) {
            hadFile = 1;

            /* read in this file */
            INIT_BUFFER(buf);
            f = fopen(arg, "r");
            if (!f) {
                perror(arg);
                return 1;
            }
            READ_FILE_BUFFER(buf, f);
            fclose(f);
            WRITE_ONE_BUFFER(buf, 0);
            cur = (const unsigned char *) buf.buf;

            /* parse it */
            pnode = sdyn_parse(cur);
            children = GGC_RP(pnode, children);

            /* load everything in */
            for (i = 0; i < children->length; i++) {
                cnode = GGC_RAP(children, i);
                name = sdyn_boxString(NULL, (char *) GGC_RD(cnode, tok).val, GGC_RD(cnode, tok).valLen);

                if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
                    /* add function to global object */
                    func = sdyn_boxFunction(cnode);
                    sdyn_setObjectMember(NULL, sdyn_globalObject, name, (SDyn_Undefined) func);

                } else if (GGC_RD(cnode, type) == SDYN_NODE_VARDECL) {
                    /* add variable to global object */
                    sdyn_getObjectMemberIndex(NULL, sdyn_globalObject, name, 1);

                } else if (GGC_RD(cnode, type) == SDYN_NODE_GLOBALCALL) {
                    /* call a global function */
                    func = (SDyn_Function) sdyn_getObjectMember(NULL, sdyn_globalObject, name);
                    sdyn_assertFunction(NULL, func);
                    sdyn_call(NULL, func, 0, NULL);

                }
            }

        } else {
            fprintf(stderr, "Use: sdyn <SDyn files>\n");
            return 1;

        }

        ARG_NEXT();
    }

    if (!hadFile) {
        fprintf(stderr, "Use: sdyn <SDyn files>\n");
        return 1;
    }

    return 0;
}
