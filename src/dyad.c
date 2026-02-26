#include "dyad.h"

DYAD dyad_table[] = {nyi, add, nyi, mlt, nyi, at, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi};

K nyi(K x, K y){NYI_ERROR(1, "dyadic operation", unref(x);unref(y))}

#define BINARY_OP(f,op) \
K f(K x, K y){ \
    K r; \
         \
    if (IS_ATOM(x)){ \
        if (IS_ATOM(y)){ \
            TYPE_ERROR(TAG_TYPE(x) != KIntType || TAG_TYPE(y) != KIntType, "", ); \
            return TAG(KIntType, TAG_VAL(x) op TAG_VAL(y)); \
        } else { \
            return f(y, x); /*swap means ops must be commutative!*/ \
        } \
    } \
 \
    if (IS_ATOM(y)){ \
        TYPE_ERROR(HDR_TYPE(x) != KIntType || TAG_TYPE(y) != KIntType, "", unref(x)) \
        r = knew(KIntType, HDR_COUNT(x)); \
        FOR_EACH(x) INT_PTR(r)[i] = INT_PTR(x)[i] op TAG_VAL(y); \
        return UNREF_X(r); \
    } \
 \
    return nyi(x,y); \
} \

BINARY_OP(add,+)
BINARY_OP(mlt,*)

K at(K x, K y){
    return UNREF_X(apply(x, 1, &y));
}