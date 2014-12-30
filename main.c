#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "sja/buffer.h"

#include "sdyn/jit.h"

int main()
{
    SDyn_Node pnode = NULL, cnode = NULL;
    SDyn_NodeArray children = NULL;
    SDyn_String name = NULL;
    SDyn_Function func = NULL;
    size_t i;
    struct Buffer_char buf;
    const unsigned char *cur;

    INIT_BUFFER(buf);
    READ_FILE_BUFFER(buf, stdin);
    WRITE_ONE_BUFFER(buf, 0);
    cur = (unsigned char *) buf.buf;

    sdyn_initValues();

    GGC_PUSH_5(pnode, cnode, children, name, func);

    pnode = sdyn_parse(cur);
    children = GGC_RP(pnode, children);
    for (i = 0; i < children->length; i++) {
        cnode = GGC_RAP(children, i);
        name = sdyn_boxString(NULL, (char *) GGC_RD(cnode, tok).val, GGC_RD(cnode, tok).valLen);

        if (GGC_RD(cnode, type) == SDYN_NODE_FUNDECL) {
            /* add function to global object */
            func = sdyn_boxFunction(cnode);
            sdyn_assertCompiled(NULL, func);
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

    return 0;
}
