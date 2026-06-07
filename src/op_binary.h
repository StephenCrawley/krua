#ifndef BINARY_H
#define BINARY_H

#include "krua.h"

typedef K (*F2)(K,K); // binary op function. f[x;y]

extern F2 binary_op[20];

K add(K, K);
K sub(K, K);
K mul(K, K);
K min(K, K);
K max(K, K);
K eql(K, K);
K at(K, K);
K join(K, K);
K take(K, K);
K drop(K, K);
K cut(K, K);

#endif
