#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>

#include "krua.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

static inline K_int int4chr(K_char *src, K_char *end){
    K_int j = 0;
    do j = j*10 + (*src++ - '0'); while (src < end && ISDIGIT(*src));
    return j;
}

#endif
