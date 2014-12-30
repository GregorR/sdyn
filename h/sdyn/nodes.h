#ifndef SDYN_NODES_H
#define SDYN_NODES_H 1

enum SDyn_NodeType {
#define SDYN_NODEX(x) SDYN_NODE_ ## x,
#include "nodex.h"
#undef SDYN_NODEX

    SDYN_NODE_LAST
};

extern char *sdyn_nodeNames[];

#endif
