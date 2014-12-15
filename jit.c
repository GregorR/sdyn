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
    size_t i, addr;
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

    /* output symbols */
    printf("$$ jit-output\n");
    addr = 0;
    for (i = 0; i < children->length; i++) {
        cnode = GGC_RAP(children, i);
        if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
            printf("  %.*s $%lx\n",
                   (int) GGC_RD(cnode, tok).valLen, (char *) GGC_RD(cnode, tok).val,
                   (unsigned long) addr);
            addr += 4096;
        }
    }

    /* including symbols for common value functions */
#define OUTSYM(nm) printf("  %s $%lx\n", #nm, (unsigned long) (void *) &nm)
    OUTSYM(sdyn_undefined);
    OUTSYM(sdyn_false);
    OUTSYM(sdyn_true);
    OUTSYM(sdyn_globalObject);
    OUTSYM(sdyn_boxBool);
    OUTSYM(sdyn_boxInt);
    OUTSYM(sdyn_boxString);
    OUTSYM(sdyn_toBoolean);
    OUTSYM(sdyn_toNumber);
    OUTSYM(sdyn_toString);
    OUTSYM(sdyn_toValue);
    OUTSYM(sdyn_assertFunction);
    OUTSYM(sdyn_getObjectMemberIndex);
    OUTSYM(sdyn_getObjectMember);
    OUTSYM(sdyn_setObjectMember);
    OUTSYM(sdyn_call);
#undef OUTSYM

    printf("$$\n");

    /* and output data */
    addr = 0;
    for (i = 0; i < children->length; i++) {
        cnode = GGC_RAP(children, i);
        if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
            unsigned char *dp;
            size_t faddr, afaddr, laddr;
            unsigned char csum;

            ir = sdyn_irCompile(cnode, NULL);
            func = sdyn_compile(ir);
            dp = (unsigned char *) (void *) func;

            for (faddr = 0; faddr < 4096; faddr += 128) {
                afaddr = addr + faddr;
                csum = 0x85 +
                       ((afaddr>>24)&0xFF) +
                       ((afaddr>>16)&0xFF) +
                       ((afaddr>>8)&0xFF) +
                       (afaddr&0xFF);
                printf("S385%08lX", (unsigned long) (addr + faddr));
                for (laddr = 0; laddr < 128; laddr++) {
                    printf("%02X", dp[faddr + laddr]);
                    csum += dp[faddr + laddr];
                }
                printf("%02X\n", (unsigned char) ~csum);
            }

            addr += 4096;
        }
    }

    return 0;
}
#endif
