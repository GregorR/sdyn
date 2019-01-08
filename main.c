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

#include "sdyn/exec.h"
#include "sdyn/jit.h"

int main(int argc, char **argv)
{
    size_t i;
    struct Buffer_char buf;
    FILE *f;
    const unsigned char *cur;
    int hadFile = 0;
    ARG_VARS;

    sdyn_initValues();

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

            sdyn_exec(cur);

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
