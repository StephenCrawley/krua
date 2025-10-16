#ifndef COMPILE_H
#define COMPILE_H

#include "skrawl.h"

enum {
    OP_MONAD=0x00,
    OP_DYAD=0x20,
    OP_ADVERB=0x40,
    OP_CONSTANT=0x46,
    OP_SET_GLOBAL,
    OP_GET_GLOBAL,
    OP_APPLY_N,
    OP_POP,
    OP_RETURN,
    OP_SET_LOCAL,
    OP_GET_LOCAL,
};

K compile(K);
K compileLambda(K,K);

#endif
