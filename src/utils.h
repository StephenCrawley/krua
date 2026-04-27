#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>

#include "krua.h"
#include "object.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

static inline K_int int4chr(K_char *src, K_char *end){
    K_int j = 0;
    do j = j*10 + (*src++ - '0'); while (src < end && ISDIGIT(*src));
    return j;
}

// 0 non-logical tail elements in the last word of a KBoolType array
// NB: this is for byte bool representation
static inline void zeroBoolTail(K x){
    K_int n = HDR_COUNT(x);
    if (n){
        K_int wn = (n+7)/8, pad = wn*8 - n;
        ((uint64_t*)x)[wn-1] = (((uint64_t*)x)[wn-1] << (pad*8)) >> (pad*8);
    }
}

static inline K notBool(K x){
    K r = knew(KBoolType, HDR_COUNT(x));
    FOR_BOOL(x){
        uint64_t word = ((uint64_t*)x)[i];
        ((uint64_t*)r)[i] = word ^ 0x0101010101010101ULL;
    }
    zeroBoolTail(r);
    return UNREF_X(r);
}

#endif
