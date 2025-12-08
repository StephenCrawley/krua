// krua common header

#ifndef KRUA_H
#define KRUA_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "error.h"
#include "limits.h"


// the K type is an unsigned int large enough to contain a pointer
// tag: for small values which are not heap-allocated, the upper 8 bits contain a type code, and the lower 32 bits contain a value
// ptr: for heap-allocated data (eg lists), K is simply a pointer to the data
// this means K encodes 2 types of objects (tags and ptrs)
// pointer objects always have a 0 upper byte, so we check this to determine what kind of object K represents
typedef uintptr_t K;

// NB: this representation assumes a 64bit system where upper bytes are unused. eg x64 only has 48bit addressable space
//     we could make the tagging scheme more general to work on 32bit or smaller systems but i'm not interested in supporting this

// NB: we use tags to represent small values for efficiency:
//     1. cache-friendly
//     2. minimizes heap fragmentation (the heap contains buddy-allocated lists.)

// NB: not all atoms (non-lists) can be contained in a tag. eg: function lambdas {..} are atomic but are contained in a pointer

// we encapsulate some fundamental K datatypes with the following typedefs
typedef uint8_t  K_char; // characters are stored in an unsigned byte
typedef int32_t  K_int;  // default integer is a signed 32 bits
typedef uint32_t K_sym;
typedef int64_t  K_long;

// NB: K_int is 32bit instead of 64bit simply for implementation simplicity:
//     64bit ints would require special handling as the full 64bit range can't be contained in a tag, so int atoms (common!) would require special handling
//     to support 64bit, a K value would need a 3rd object type: a tagged pointer where the upper bits contain type (KLongType) and the lower bits are a pointer to the value
//     this is not hard to implement, but i'm not interested in supporting this right now

// K type enum (remember: update KWIDTHS after adding a type)
enum {
    KObjType = 0,
    KChrType,
    KIntType,
    KSymType,
    KMonadType,
    K_GENERIC_TYPES_START,
    KLambdaType = K_GENERIC_TYPES_START,
};

// when K is a pointer, it points to the start of a heap-allocated list
// directly ahead of this list is the array header, which contains some metadata (type, refcount, membucket, listcount, etc)
typedef struct {
    K_char  a;  // a(attribute/argcount. for lists/lambdas respectively)
    K_char  m;  // count of locals
    K_char  b;  // memory bucket
    K_char  t;  // type
    K_int   r;  // refcount
    K_int   n;  // count
} K_hdr;

// we access heap-allocated K arrays with the following:
#define K_HDR(x)      ((K_hdr*)(x))[-1]
#define HDR_ARGC(x)   K_HDR(x).a
#define HDR_VARC(x)   K_HDR(x).m
#define HDR_BUCKET(x) K_HDR(x).b
#define HDR_TYPE(x)   K_HDR(x).t
#define HDR_REFC(x)   K_HDR(x).r
#define HDR_COUNT(x)  K_HDR(x).n
#define OBJ_PTR(x)    ((     K*)(x))
#define CHR_PTR(x)    ((K_char*)(x))
#define INT_PTR(x)    (( K_int*)(x))
#define LNG_PTR(x)    ((K_long*)(x))
#define SYM_PTR(x)    INT_PTR(x)

// we inspect and access tagged K objects with:
#define IS_TAG(x)   ((x) >> 56)
#define TAG_TYPE(x) ((x) >> 56)
#define TAG_VAL(x)  ((K_int)(x))
#define TAG(t,x)    ((x) | (K)(t)<<56) //create a tag

// dict / table access
#define KEYS(k)     OBJ_PTR(k)[0]
#define VALS(k)     OBJ_PTR(k)[1]

// some useful utility macros:
#define MEMCPY(d, s, n) (K)memcpy((void*)(d), (void*)(s), n)
#define WIDTH_OF(x)     KWIDTHS[HDR_TYPE(x)]
#define PTR_TO(x, i)    ({ K _x=(x); _x + (i)*WIDTH_OF(_x); })
#define IS_ATOM(x)      ({ K _x=(x); IS_TAG(_x)||HDR_TYPE(_x)==KLambdaType ;}) //TODO: can we group type enums so atomics are contiguous?
#define IS_NESTED(x)    ({ K_char _t=HDR_TYPE(x); !_t || _t>=K_GENERIC_TYPES_START ;})
// TODO: EXPR/LAMBDA ACCESS (bytecode; vars; consts; source)

// some useful global data is here:

// width of each type's items
//                      Obj, Chr, Int, Sym, Monad, Lambda
static int KWIDTHS[] = {  8,   1,   4,   4,     8,      8};

// some useful macros are here:

// K is list-oriented language, and we regularly need to iterate over each list element
// the FOR_EACH macro simply removes some clutter and clearly shows that each list element is iterated
#define FOR_EACH(x) for (K_int i=0, _n=HDR_COUNT(x); i<_n; ++i)

// helpful test macro
#define DEBUG(s,args...) putchar('\n'), printf(__func__), printf(": " s"\n", ##args),fflush(stdout)

#endif