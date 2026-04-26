#ifndef UNARY_H
#define UNARY_H

#include "krua.h"

typedef K (*F1)(K); // unary operator f[x]

extern F1 unary_op[21];

K neg(K);
K value(K);
K where(K);
K til(K);
K count(K);
K csv(K);

#endif
