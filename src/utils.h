#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>

#include "krua.h"
#include "object.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

static inline K_char chr4chr(K_int n, K_char *s){
    return n==0 ? ' ' : *s;
}

static inline K_int int4chr(K_int n, K_char *s){
    if (*s == '-') return -int4chr(n-1, ++s);
    K_int j = 0;
    FOR(n) j = j*10 + (*s++ - '0');
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
