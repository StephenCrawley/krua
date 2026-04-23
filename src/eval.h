#ifndef PARSE_H
#define PARSE_H

#include "krua.h"
#include "object.h"
#include "limits.h"
#include "op_unary.h"
#include "op_binary.h"
#include "file.h"

enum {
    OP_UNARY   = 0x00,
    OP_BINARY  = 0x20,
    OP_N_ARY   = 0x40,
    OP_CONST   = 0x60,
    OP_GET_VAR = 0x80,
    OP_SET_VAR = 0xa0,
    OP_VERB    = 0xc0, // push operatory / adverb wrap
    OP_SPECIAL = 0xe0, // special op codes
    OP_POP     = OP_SPECIAL + 0,
    OP_ENLIST  = OP_SPECIAL + 1,
};

#define IS_CLASS(class, b) (b-class < 32u)
#define IS_OPERATOR(x) ((x) < 20u)  // raw operators. see OPS
#define IS_PRIMITIVE(x) ((x) < ADVERB_START) // operator + keywords
#define ADVERB_START 29u

extern K GLOBALS; // global interpreter state
extern K_char KEYWORDS_STRING[]; // unary primitive keywords string
extern K KEYWORDS; // unary primitive keywords symlist

K token(K,K*,K*);
K compile(K, K, int);
K getGlobal(K_sym);
K vm(K x, K vars, K consts, K_char localc, K*args);
void strip(K);
K eval(K);

#endif