#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>

#include "krua.h"
#include "object.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

static inline K_char chr4chr(K_char*s, K_char*e){
    return s==e ? ' ' : *s;
}

static inline K_int int4chr(K_char *src, K_char *end){
    K_int j = 0;
    do j = j*10 + (*src++ - '0'); while (src < end);
    return j;
}

// 0 non-logical tail elements in the last word of a KBoolType array
// NB: this is for bit bool representation
static inline void zeroBoolTail(K x){
    K_int n = HDR_COUNT(x);
    if (n & 63){
        K_int wn = (n+63)/64;
        ((uint64_t*)x)[wn-1] &= (1ULL << (n & 63)) - 1;
    }
}

static inline K notBool(K x){
    K r = reuse(KBoolType, x);
    FOR_WORDS(x){
        uint64_t word = ((uint64_t*)x)[i];
        ((uint64_t*)r)[i] = ~word;
    }
    zeroBoolTail(r);
    return UNREF_X(r);
}

#endif
