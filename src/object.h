#ifndef OBJECT_H
#define OBJECT_H

#include "krua.h"

#define MIN_ALLOC 128UL // minimum bytes per object. size allows overreads (eg SIMD chunks)
#define HDR_PAD    64UL
#define BUCKET_SIZEOF(x) (MIN_ALLOC << HDR_BUCKET(x))  // size of the bucket that x is in

#define UNREF_X(k)  ({__typeof__(k)_k=(k); unref(x); _k;})
#define UNREF_Y(k)  ({__typeof__(k)_k=(k); unref(y); _k;})
#define UNREF_R(k)  ({__typeof__(k)_k=(k); unref(r); _k;})
#define UNREF_XY(k) UNREF_X(UNREF_Y(k))
#define UNREF_XR(k) UNREF_X(UNREF_R(k))

K ref(K);
void _unref(K);
K_sym encodeSym(K_char*, int);
K syms4chrs(K);
K_char addSym(K*, K_sym);
K* getSlot(K, K_sym);
K_int findSym(K, K_sym);
K _knew(K_char, K_int);
K reuse(K_char, K);
K knewcopy(K_char, K_int, K);
K k1(K);
K k2(K, K);
K k3(K, K, K);
K k4(K, K, K, K);
K kstr(K_int, K_char*);
K kcstr(const char*);
K ksymdict();
K kc1(K_char);
K kc2(K_char, K_char);
K cutStr(K, K_char);
K joinStr(K, K_char);
static inline K razeStr(K x){ return joinStr(x, 0); }
K joinTag(K, K);
K joinObj(K, K);
K joinList(K, K);
K squeeze(K);
K expand(K);
K item(K_int, K);
K promote(int, K);
K kprint(K);

static inline K kchr(K_char c) { return TAG(KChrType, c); }
static inline K kint(K_int  i) { return TAG(KIntType, i); }
static inline K klong(K_long i){ return i; } // TODO: heap allocation when full KLngType support added
static inline K kop(K_int   i) { return TAG(KOpType, i); }
#define knull() kop(0)
static inline K kadverb(K x,K_int i) { K a=k1(x); return HDR_TYPE(a)=KAdverbType, HDR_ADVERB(a)=i, a; }

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