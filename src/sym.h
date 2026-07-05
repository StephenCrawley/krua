#ifndef SYM_H
#define SYM_H

#include "krua.h"

extern K HTAB; // hash tab
extern K SYMS; // sym pool

void initSymTab();
void freeSymTab();
K_sym internSym(K_int, K_char*);

#endif
