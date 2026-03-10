#ifndef BINARY_H
#define BINARY_H

#include "krua.h"
#include "object.h"
#include "op_unary.h"
#include "apply.h"

typedef K (*F2)(K,K); // binary op function. f[x;y]

extern F2 binary_op[20];

K add(K, K);
K sub(K, K);
K mlt(K, K);
K eql(K, K);
K at(K, K);

#endif
