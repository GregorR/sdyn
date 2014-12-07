#define _BSD_SOURCE /* for MAP_ANON */

#include "sdyn/jit.h"

#define USE_SJA_SHORT_NAMES 1
#include "sja/sja.h"

#if defined(SJA_ARCH_X86_64)
#include "jit-x8664.c"

#else
#error Unsupported architecture!

#endif

#ifdef USE_SDYN_JIT_TEST
int main()
{
    SDyn_Node pnode = NULL, cnode = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_IRNodeArray ir = NULL;
    sdyn_native_function_t func;
    size_t i;
    struct Buffer_char buf;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    sdyn_initValues();

    GGC_PUSH_4(pnode, cnode, children, ir);

    pnode = sdyn_parse(cur);
    children = GGC_RP(pnode, children);
    for (i = 0; i < children->length; i++) {
        cnode = GGC_RAP(children, i);
        if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
            ir = sdyn_irCompile(cnode, NULL);
            func = sdyn_compile(ir);
            fwrite((void *) func, 1, 4096, stdout);
        }
    }

    return 0;
}
#endif
