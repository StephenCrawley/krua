#ifndef PARSE_H
#define PARSE_H

#include <ctype.h>

#include "krua.h"
#include "object.h"
#include "limits.h"
#include "dyad.h"

enum {
    OP_UNARY   = 0x00,
    OP_BINARY  = 0x20,
    OP_N_ARY   = 0x40,
    OP_CONST   = 0x60,
    OP_GET_VAR = 0x80,
    OP_SET_VAR = 0xa0,
};

#define ISCLASS(class, b) ({K_char _b=(b); _b-class < 32u;})

// Global interpreter state
extern K GLOBALS;

K token(K,K*,K*);
K compileExpr(K);
K getGlobal(K, K_sym);
K vm(K x, K vars, K consts, K GLOBALS, K_char localc, K*args);
void strip(K);
K eval(K,K);

#endif