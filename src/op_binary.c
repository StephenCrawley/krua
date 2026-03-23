#include "op_binary.h"

// iteration helpers over binary operators

// f'[x;y]
static K _each2(F2 f, K x, K y){
    LENGTH_ERROR(HDR_COUNT(x) != HDR_COUNT(y), "", unref(x); unref(y));
    K r = knew(KObjType, HDR_COUNT(x)), *robj = OBJ_PTR(r);
    FOR_EACH(r){
        K t = f(item(i, x), item(i, y));
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        robj[i] = t;
    }
    return UNREF_XY(r);
}

// x f\: y
static K _eachleft(F2 f, K x, K y){
    K r = knew(KObjType, HDR_COUNT(x)), *robj = OBJ_PTR(r);
    FOR_EACH(r){
        K t = f(item(i, x), ref(y));
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        robj[i] = t;
    }
    return UNREF_XY(r);
}

// binary ops

K nyi(K x, K y){NYI_ERROR(1, "binary operator", unref(x);unref(y))}

//                :    +    -    *    %    &    |    <    >    =    @   .    !    ,    ?    #    _    ~    $    ^
F2 binary_op[] = {nyi, add, sub, mlt, nyi, min, max, nyi, nyi, eql, at, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi, nyi};

#define BINARY_OP(f,op) \
K f(K x, K y){ \
    K r; \
    if (IS_TAG(x)){ \
        if (IS_TAG(y)){ \
            TYPE_ERROR(MAX(TAG_TYPE(x),TAG_TYPE(y)) > KIntType, "", ); \
            return TAG(KIntType, op(TAG_VAL(x), TAG_VAL(y))); \
        } \
        return f(y, x); /* swap means ops must be commutative! */ \
    } \
    if (HDR_TYPE(x) == KObjType || (!IS_TAG(y) && HDR_TYPE(y) == KObjType)) return (IS_TAG(y) ? _eachleft : _each2)(f, x, y); \
    if (!(x = promote(KIntType, x))) return UNREF_Y(0); \
    if (IS_TAG(y)){ \
        TYPE_ERROR(TAG_TYPE(y) > KIntType, "", unref(x)) \
        r = knew(KIntType, HDR_COUNT(x)); \
        FOR_EACH(x) INT_PTR(r)[i] = op(INT_PTR(x)[i], TAG_VAL(y)); \
        return UNREF_X(r); \
    } \
    LENGTH_ERROR(HDR_COUNT(x) != HDR_COUNT(y), "", unref(x); unref(y)) \
    if (!(y = promote(KIntType, y))) return UNREF_X(0); \
    r = knew(KIntType, HDR_COUNT(x)); \
    FOR_EACH(x) INT_PTR(r)[i] = op(INT_PTR(x)[i], INT_PTR(y)[i]); \
    return UNREF_XY(r); \
} \

#define ADD(x,y) ((x)+(y))
#define MLT(x,y) ((x)*(y))
#define EQL(x,y) ((x)==(y))
BINARY_OP(add,ADD)
K sub(K x, K y){ K r; return (r=neg(y)) ? add(x,r) : UNREF_X(r); }
BINARY_OP(mlt,MLT)
BINARY_OP(min,MIN)
BINARY_OP(max,MAX)
BINARY_OP(eql,EQL)

K at(K x, K y){
    return UNREF_X(apply(x, 1, &y));
}
