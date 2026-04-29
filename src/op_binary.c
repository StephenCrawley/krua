#include "op_binary.h"
#include "object.h"
#include "op_unary.h"
#include "apply.h"
#include "utils.h"
#include "error.h"

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

#define ADD(x,y) ((x)+(y))
//#define SUB(x,y) ((x)-(y)) // currently dead code
#define MLT(x,y) ((x)*(y))
#define EQL(x,y) ((x)==(y))

// list-list loop
#define LL(T, E) { \
    T *rp=(T*)r, *xp=(T*)x, *yp=(T*)y; \
    for (K_int i=0; i<n; i++) rp[i] = E(xp[i], yp[i]); } \

// list-atom loop
#define LA(T, E) { \
    T *rp=(T*)r, *xp=(T*)x; T b=TAG_VAL(y); \
    for (K_int i=0; i<n; i++) rp[i] = E(xp[i], b); } \

// comparison list-list (returns KBoolType)
#define CL(T, E) { \
    K_char *rp=(K_char*)r; T *xp=(T*)x, *yp=(T*)y; \
    for (K_int i=0; i<n; i++) rp[i] = E(xp[i], yp[i]); \
    zeroBoolTail(r); } \

// comparison list-atom loop (returns KBoolType)
#define CA(T, E) { \
    K_char *rp=(K_char*)r; T *xp=(T*)x; T b=TAG_VAL(y); \
    for (K_int i=0; i<n; i++) rp[i] = E(xp[i], b); \
    zeroBoolTail(r); } \

// dispatch LL-LA
#define LY(T, E) { if (IS_TAG(y)) LA(T, E) else LL(T, E) }
// dispatch CL-CA
#define CY(T, E) { if (IS_TAG(y)) CA(T, E) else CL(T, E) }

#define LC(T) case 9:CY(T,EQL);break;
#define LX(T) switch(op){case 1: LY(T,ADD);break; case 3:LY(T,MLT);break; case 5:LY(T,MIN);break; case 6:LY(T,MAX); break; LC(T)}

#define VSWITCH() switch(t){case KBoolType: case KChrType: switch(op){case 5:LY(K_char,MIN);break; case 6:LY(K_char,MAX); break; LC(K_char)} break; case KIntType: LX(K_int) break;}

static K binaryDispatch(int op, K x, K y){
    // first promote args to the wider type. binary ops work on same types. arith always promotes to int. comp promotes to max of args x,y
    K_char t = op < 5 ? KIntType : MAX(HDR_TYPE(x), IS_TAG(y) ? TAG_TYPE(y) : HDR_TYPE(y));
    if (!IS_TAG(y)){
        LENGTH_ERROR(HDR_COUNT(y) != HDR_COUNT(x), "", unref(x); unref(y));
        if (!(y = promote(t, y))){ unref(x); return 0; }
    }
    if (!(x = promote(t, x))){ unref(y); return 0; }
    // then init the return object
    K_int n = HDR_COUNT(x);
    // op 7-9 comparison, returns bool. op<7 arithmetic, x always promoted to int, and always returns int
    K r = op < 7 ? reuse(t, x) : knew(KBoolType, n);
    VSWITCH();
    return UNREF_XY(r);
}

#define BINARY_OP(f,g,op) \
K f(K x, K y){ \
    if (IS_TAG(x)){ \
        if (IS_TAG(y)){ \
            K_char t = MAX(TAG_TYPE(x),TAG_TYPE(y)); \
            TYPE_ERROR(t >= KNumericEndType, "", ); \
            return TAG(op < 5 ? KIntType : op < 7 ? t : KBoolType, g(TAG_VAL(x), TAG_VAL(y))); \
        } \
        return f(y, x); /* swap means op must be commutative! */ \
    } \
    if (IS_TAG(y)){ \
        return HDR_TYPE(x) ? binaryDispatch(op, x, y) : _eachleft(f, x, y); \
    } \
    return HDR_TYPE(x) != KObjType && HDR_TYPE(y) != KObjType ? binaryDispatch(op, x, y) : _each2(f, x, y); \
} \

BINARY_OP(add,ADD,1)
K sub(K x, K y){ K r; return (r=neg(y)) ? add(x,r) : UNREF_X(r); }
BINARY_OP(mlt,MLT,3)
BINARY_OP(min,MIN,5)
BINARY_OP(max,MAX,6)
BINARY_OP(eql,EQL,9)

K at(K x, K y){
    return UNREF_X(apply(x, 1, &y));
}
