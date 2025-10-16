#ifndef OBJECT_H
#define OBJECT_H

#include "skrawl.h"

#define UNREF_X(a)   __extension__({K _a=(a); unref(x), _a;})
#define UNREF_Y(a)   __extension__({K _a=(a); unref(y), _a;})
#define UNREF_R(a)   __extension__({K _a=(a); unref(r), _a;})
#define UNREF_T(a)   __extension__({__typeof__(a)_a=(a); unref(t), _a;})
#define UNREF_XY(a)  __extension__({K _a=(a); unref(x), unref(y), _a;})
#define UNREF_XR(a)  __extension__({K _a=(a); unref(x), unref(r), _a;})
#define UNREF_XYR(a) __extension__({K _a=(a); unref(x), unref(y), unref(r), _a;})
#define   REF_N_OBJS(x,n) {K*_x=(x);i64 _n=(n); for (i64 i=0; i<_n; i++)   ref(_x[i]);}
#define UNREF_N_OBJS(x,n) {K*_x=(x);i64 _n=(n); for (i64 i=0; i<_n; i++) unref(_x[i]);}

// lambda accessors
// (bytecode; consts; literal sting; parameters; locals)
#define LAMBDA_OPCODE(x)  ( OBJ(x)[0] )
#define LAMBDA_CONSTS(x)  ( OBJ(x)[1] )
#define LAMBDA_STRING(x)  ( OBJ(x)[2] )
#define LAMBDA_PARAMS(x)  ( OBJ(x)[3] )
#define LAMBDA_LOCALS(x)  ( OBJ(x)[4] )
#define LAMBDA_GLOBALS(x) ( OBJ(x)[5] )
// this is for the compiler and VM 
#define IS_LAMBDA(x) (HDR_CNT(x) == 6)

// public object functions
void unref(K);
K ref(K);
void replace(K*,K);
K tn(i8,i64);
K j2(K,K);
K jk(K,K);
i8 ksize(K);
K va(K);
K k1(K);
K k2(K,K);
K k3(K,K,K);
K kn(K*,i64);
K kx(u8);
K kx2(u8,u8);
K kc(u8);
K kCn(char*,i64);
K kC0(char*);
K ki(i64);
K kf(double);
K ks(i64);
K kD(K,K);
K kT(K);
K ku(u64);
K kv(u64);
K kuc(char);
K kvc(char);
K kw(i8);
K kwx(i8,K);
K kq(K,K);
K km();
K ke(K);
K kerr(char*);
K squeeze(K);
K expand(K);
K enlist(K);
K item(i64,K);
K sublist(K,i64,i64);
K reuse(K);
K printK(K);
u8 cverb(u8);
u8 cadverb(u8);
u64 iadverb(char);

static inline K knul(){ return SET_TAG(KN,0); }

#ifdef DBG_WS
extern u64 WS;
extern u64 WT;
#endif

#endif
