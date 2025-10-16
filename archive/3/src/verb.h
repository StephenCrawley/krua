#ifndef VERB_H
#define VERB_H

#include "skrawl.h"

typedef K (*MONAD)(K);
typedef K (*DYAD)(K,K);

extern MONAD monad_table[];
extern DYAD dyad_table[];

// monadic operator functions
K identity(K);
K flip(K);
K neg(K);
K first(K);
K ksqrt(K);
K distinct(K);
K value(K);
K type(K);
K til(i64);
K getKey(K);
K string(K);
K count(K);
K lower(K);
K isNull(K);
K where(K);
K group(K);
K gradeUp(K);
K gradeDown(K);
K not(K);
K reverse(K);

// dyadic operator functions
K dex(K,K);
K add(K,K);
K subtract(K,K);
K multiply(K,K);
K divide(K,K);
K join(K,K);
K findSym(K,K);
K find(K,K);
K dotApply(K,K);
K atApply(K,K);
K makeKey(K,K);
K cast(K,K);
K take(K,K);
K n_take(i64,K);
K drop(K,K);
K fill(K,K);
K min(K,K);
K equal(K,K);
K lessThan(K,K);
K greaterThan(K,K);
K match(K,K);
K max(K,K);
K splitString(K,K);

#endif
