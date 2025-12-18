#ifndef OBJECT_H
#define OBJECT_H

#include "krua.h"

#define UNREF_X(k)  ({__typeof__(k)_k=(k); unref(x); _k;})
#define UNREF_Y(k)  ({__typeof__(k)_k=(k); unref(y); _k;})
#define UNREF_R(k)  ({__typeof__(k)_k=(k); unref(r); _k;})
#define UNREF_XY(k) UNREF_X(UNREF_Y(k))
#define UNREF_XR(k) UNREF_X(UNREF_R(k))

K ref(K);
void _unref(K);
//K kchr(K_char);
//K kint(K_int);
static inline K kchr(K_char c) { return TAG(KChrType, c); }
static inline K kint(K_int  i) { return TAG(KIntType, i); }
static inline K knull()        { return TAG(KMonadType,0); }
K_sym encodeSym(K_char*, int);
K_char addSym(K*, K_sym);
K* getSlot(K, K_sym);
K_int findSym(K, K_sym);
K _knew(K_char, K_int);
K knewcopy(K_char, K_int, K);
K k1(K);
K k2(K, K);
K k3(K, K, K);
K k4(K, K, K, K);
K kcstr(const char*);
K ksymdict();
K kc1(K_char);
K kc2(K_char, K_char);
K razeStr(K x);
K cutStr(K, K_char);
K joinStr(K, K_char);
K joinTag(K, K);
K joinObj(K, K);
K squeeze(K);
K kprint(K);

#ifdef TRACK_REFS
  // When tracking, include the tracking header
  #include "../tests/refcount.h"
  // refcount.h defines:
  //   #define knew(t,n) track_knew(t, n, __FILE__, __LINE__)
  //   #define unref(x) track_unref(x, __FILE__, __LINE__)
#else
  // When not tracking, direct to real functions
  #define knew _knew
  #define unref _unref
#endif

#endif