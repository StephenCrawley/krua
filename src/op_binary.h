#ifndef BINARY_H
#define BINARY_H

#include "krua.h"

typedef K (*F2)(K,K); // binary op function. f[x;y]

extern F2 binary_op[20];
static inline K binop(K f, K x, K y){ return binary_op[TAG_VAL(f)](x, y); }
#define IS_ATOMIC_BINOP(f) (TAG_TYPE(f) == KOpType && TAG_VAL(f) < 10)

K add(K, K);
K sub(K, K);
K mul(K, K);
K min(K, K);
K max(K, K);
K ltn(K, K);
K mtn(K, K);
K eql(K, K);
K at(K, K);
K join(K, K);
K take(K, K);
K drop(K, K);
K cut(K, K);

#endif
