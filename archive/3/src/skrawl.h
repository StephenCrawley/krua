#ifndef SKRAWL_H
#define SKRAWL_H

// shared includes
#include <stdio.h>
#include <stdlib.h>    //mmap
#include <inttypes.h>
#include <string.h>    //memcpy
#include <stdbool.h>

// type macros
typedef uint64_t  K;
typedef int8_t    i8;
typedef uint8_t   u8;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef int64_t   i64;
typedef uint64_t  u64;

// K object types
enum {
    KK,                        //generic list
    KX,                        //byte
    KC,                        //char
    KI,                        //int64
    KF,                        //double
    KS,                        //symbol
    K_SIMPLE_LIST_END,         //simple lists are composed of same-type atoms
    KD = K_SIMPLE_LIST_END,    //dict
    KT,                        //table
    K_INDEXABLE_END,           //end of types that can subscripted
    KL = K_INDEXABLE_END,      //lambda
    KP,                        //projection
    KQ,                        //composition
    K_ADVERB_START,            //start of adverb types
    KEACH = K_ADVERB_START,    // '
    KOVER,                     // /
    KSCAN,                     /* \ */ 
    KEP,                       // ':
    KER,                       // /:
    KEL,                       // \:        
    K_ADVERB_END,              //end of adverb types
    KU = K_ADVERB_END,         //monad
    KV,                        //dyad
    KW,                        //adverb
    KM,                        //magic value(internal)
    KN,                        //null(internal)
    KE                         //error(internal)
};

// TOKEN ENUM
// :+-*%,?.@!$#_^&=<>~|0
enum {
    TOK_COLON,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_DIV,
    TOK_COMMA,
    TOK_QM,
    TOK_DOT,
    TOK_AT,
    TOK_BANG,
    TOK_DOLLAR,
    TOK_HASH,
    TOK_UNDERSCORE,
    TOK_CARET,
    TOK_AND,
    TOK_EQ,
    TOK_LESSTHAN,
    TOK_MORETHAN,
    TOK_TILDE,
    TOK_PIPE,
    TOK_ZERO,
    TOK_SEMICOLON,
};

// K header accessors
// the header is 16 bytes
// -rmtrrrrnnnnnnnn
// - unused, r rank, m membucket, t type, r refcount, n count
#define HDR_RNK(x)    (( i8*)(x))[-15]
#define HDR_MEM(x)    (( i8*)(x))[-14]
#define HDR_TYP(x)    (( i8*)(x))[-13]
#define HDR_REF(x)    ((u32*)(x))[-3]
#define HDR_CNT(x)    ((i64*)(x))[-1]
// tagged pointer type get/set. type encoded in upper 8 bits
#define TAG_TYP(x)    ((i8)((x)>>56))
#define TAG_VAL(x)    ((x) & 0x00ffffffffffffff)
#define SET_TAG(t,v)  (K)((((i64)(t))<<56) | (0x00ffffffffffffff & (i64)(v)))
// wrapper accessors
#define MEM(x)  HDR_MEM(x)  
#define TYP(x)  __extension__({K _a=(x); TAG_TYP(_a) ? TAG_TYP(_a) : HDR_TYP(_a);})  
#define REF(x)  HDR_REF(x)  
#define CNT(x)  __extension__({K _a=(x); TAG_TYP(_a) ? 1 : HDR_CNT(_a);})

// K object accessors
#define OBJ(x)  ((     K*)(x))  //pointer to generic K object list
#define CHR(x)  ((    u8*)(x))  //pointer to int8 
#define INT(x)  ((   i64*)(x))  //pointer to int64 
#define FLT(x)  ((double*)(x))  //pointer to double 

// K null values
#define CNULL  (' ')
#define INULL  ((i64)0x8000000000000000LL)
#define FNULL  (0/0.0)
#define SNULL  0

// shared utility macros
#define ABS(a)             __extension__({__typeof__(a)_a=(a); _a > 0 ? _a : -_a ;}) 
#define MAX(a,b)           __extension__({__typeof__(a)_a=(a);__typeof__(b)_b=(b);_a>_b?_a:_b;})
#define MIN(a,b)           __extension__({__typeof__(a)_a=(a);__typeof__(b)_b=(b);_a<_b?_a:_b;})
#define KEY(x)             OBJ((x))[0] //dict key
#define VAL(x)             OBJ((x))[1] //dict value
#define KCOUNT(x)          __extension__({K _b=(x); i8 t=TYP(_b); IS_ATOM(_b)?1:HDR_CNT(t==KD?VAL(_b):t==KT?*OBJ(VAL(*OBJ(_b))):_b);})
#define IS_ATOM(x)         __extension__({K _x=(x); i8 t=TYP(_x); t<0 || t>=K_INDEXABLE_END;})
#define IS_SIMPLE_LIST(x)  __extension__({K _x=(x); !TAG_TYP(_x) && HDR_TYP(_x)>0 && HDR_TYP(_x)<K_SIMPLE_LIST_END;})
#define IS_VERB(a)         __extension__({i8 t=TYP(a); KU==t || KV==t;}) 
#define IS_DERIVED_VERB(a) __extension__({i8 t=TYP(a); K_ADVERB_START<=t && t<K_ADVERB_END;})
#define IS_GENERIC(x)      __extension__({i8 t=TYP(x); !t || (t>=K_SIMPLE_LIST_END && t<K_ADVERB_END);}) //has other K objects as children
#define IS_ERROR(x)        (TYP((x))==KE)
#define IS_NULL(x)         (TAG_TYP((x))==KN)
#define IS_OP(x,t,v)       (SET_TAG(t,v)==(x))
#define IS_MONAD(x,v)      IS_OP((x),KU,v)
#define IS_DYAD(x,v)       IS_OP((x),KV,v)
#define IS_MAGIC_VAL(x)    IS_OP((x),KM,0)
// shared utility functions
static inline char* sc(char *s,char c){ while(*s!=c)if(!*s++)return (char*)0; return s; }
static inline i64   ic(char *s,char c){ return sc(s,c)-s; }
static inline K     tx(i8 t,K x){ return TAG_TYP(x) ? SET_TAG(t,x) : (HDR_TYP(x)=t,x); }
static inline i64   symIndex(K x,K y){
    i64 i=0,yi=*INT(y),xn=HDR_CNT(x),*xptr=INT(x); 
    for (; i<xn; i++)
        if (yi == xptr[i]) break; 
    return i;
}
static inline i8    rankOf(K x){
    i8 t=TYP(x);
    // @ and . are variadic
    if (IS_DYAD(x,TOK_AT) || IS_DYAD(x,TOK_DOT))
        return -4;
    if (t == KW && (TAG_VAL(x) == (u64)(KEACH-K_ADVERB_START)))
        return -2;
    return (t>=K_INDEXABLE_END && t<K_ADVERB_END) ? HDR_RNK(x) : t==KV ? 2 : 1;
}

#endif
