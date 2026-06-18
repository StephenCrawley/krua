#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>

#include "krua.h"
#include "object.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))

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

// relies on zeroed tail beyond logical length (bool tail invariant)
static inline K_int sumBools(K x){
    K_int n = 0;
    FOR_WORDS(x) n += stdc_count_ones(((uint64_t*)x)[i]);
    return n;
}

static inline K_int sumInts(K x){
    K_int j = 0;
    FOR_EACH(x) j += INT_PTR(x)[i];
    return j;
}

#endif
