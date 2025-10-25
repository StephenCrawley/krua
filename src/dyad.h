#ifndef DYAD_H
#define DYAD_H

#include "krua.h"
#include "object.h"
#include "apply.h"

typedef K (*DYAD)(K,K);

extern DYAD dyad_table[20];

K nyi(K, K);
K add(K, K);
K mlt(K, K);
K at(K, K);

#endif