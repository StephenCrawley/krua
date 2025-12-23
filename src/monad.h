#ifndef MONAD_H
#define MONAD_H

#include "krua.h"
#include "object.h"

typedef K (*MONAD)(K);

extern MONAD monad_table[20];

K value(K);

#endif