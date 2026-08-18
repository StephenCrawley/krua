#include "adverb.h"
#include "object.h"
#include "apply.h"
#include "eval.h"
#include "op_unary.h"
#include "op_binary.h"
#include "utils.h"
#include "error.h"

// forward declarations
K each1(K, K);
K each1Generic(K, K);
K each2(K, K, K);
K each2Generic(K, K, K);
K eachright1(K, K);
K eachleft1(K, K);
static K eachright2(K, K, K);
static K eachleft2(K, K, K);
K over1(K, K);
K over1Generic(K, K);
K over1Bool(K, K);
K over1Int(K, K);
K over2(K, K, K);
K scan1(K, K);
K scan1Generic(K, K);
K sumsBools(K);
K scan2(K, K, K);
K prior1(K, K);
K prior2(K, K, K);

// dispatch

// f'x f/x f\x f':x
K adv1(K f, K x){
    RANK_ERROR(IS_ATOM(x), "f'atom", unref(x));
    return PICK6(HDR_ADVERB(f), each1, over1, scan1, prior1, eachright1, eachleft1)(OBJ_PTR(f)[0], x);
}

// f'[x;y] f/[x;y] f\[x;y] x f/:y x f\:y
K adv2(K f, K x, K y){
    return PICK6(HDR_ADVERB(f), each2, over2, scan2, prior2, eachright2, eachleft2)(OBJ_PTR(f)[0], x, y);
}

// function pointer kernels: the adverb hot path, bypassing apply().
// ops call these for atomic extension over nested lists (-x is -'x, x+y is +'[x;y], x+atom is x+\:atom)

K _each1(F1 f, K x){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = f(item(i, x));
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X(r);
}

// f'[x;y]
K _each2(F2 f, K x, K y){
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
K _eachleft(F2 f, K x, K y){
    K r = knew(KObjType, HDR_COUNT(x)), *robj = OBJ_PTR(r);
    FOR_EACH(r){
        K t = f(item(i, x), ref(y));
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        robj[i] = t;
    }
    return UNREF_XY(r);
}

// each (map)

K each1(K f, K x){
    if (TAG_TYPE(f) != KOpType) return each1Generic(f, x);
    return TAG_VAL(f)==2  ? neg(x)      // unary - and ~ already iterate,
         : TAG_VAL(f)==17 ? not(x)      // so no need to use 'each' machinery
         : squeeze(_each1(unary_op[TAG_VAL(f)], x));
}

K each1Generic(K f, K x){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = item(i, x);
        t = apply(f, 1, &t);
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X( squeeze(r) );
}

// f/:x
K eachright1(K f, K x){
    (void)f;
    NYI_ERROR(1, "eachright1", UNREF_X(0));
}

// x f/: y
static K eachright2(K f, K x, K y){
    RANK_ERROR(IS_ATOM(y), "x f/: yatom", UNREF_XY(0));
    if (IS_ATOM(x) && IS_ATOMIC_BINOP(f)) return binop(f, x, y);
    K r = knew(KObjType, HDR_COUNT(y));
    FOR_EACH(r){
        K t = apply(f, 2, (K[]){ref(x), item(i, y)});
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_XY(squeeze(r));
}

// f\:x
K eachleft1(K f, K x){
    (void)f;
    NYI_ERROR(1, "eachleft1", UNREF_X(0));
}

// x f\: y
static K eachleft2(K f, K x, K y){
    RANK_ERROR(IS_ATOM(x), "xatom f\\: y", UNREF_XY(0));
    if (IS_ATOM(y) && IS_ATOMIC_BINOP(f)) return binop(f, x, y);
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(r){
        K t = apply(f, 2, (K[]){item(i, x), ref(y)});
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_XY(squeeze(r));
}

K each2(K f, K x, K y){
    if (IS_ATOM(x)) return eachright2(f, x, y);
    if (IS_ATOM(y)) return eachleft2(f, x, y);
    return IS_ATOMIC_BINOP(f) ? binop(f, x, y) : each2Generic(f, x, y);
}

K each2Generic(K f, K x, K y){
    LENGTH_ERROR(HDR_COUNT(x) != HDR_COUNT(y), "f'[x;y]", UNREF_XY(0));
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = apply(f, 2, (K[]){item(i, x), item(i, y)});
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_XY( squeeze(r) );
}

// over (reduce)

K over1(K f, K x){
    return (TAG_TYPE(f) == KOpType ? // specialized kernels for some reductions
            HDR_TYPE(x) == KBoolType && TAG_VAL(f)-1u < 6u ? over1Bool : HDR_TYPE(x) == KIntType && TAG_VAL(f)-1u < 3u ? over1Int : over1Generic : 
            over1Generic)(f, x);
}

// specialized kernels (sumInts is in utils.h)

K over1Bool(K f, K x){
    K_int j = sumBools(x);
    switch (TAG_VAL(f)){
    case 1: /* nothing to do */ ; break; // +
    case 2: j = GET_BIT(x,0)*2 - j; break; // -
    case 3: /* fallthrough */
    case 4: /* fallthrough */
    case 5: j = j == HDR_COUNT(x); break; // * % &. div here is an implementation quirk: it is NYI thru every other path
    case 6: j = j>0; break; // |
    }
    return UNREF_X(TAG(TAG_VAL(f) < 5 ? KIntType : KBoolType, j));
}

// -/x
K_int subInts(K x){
    if (!HDR_COUNT(x)) return 0;
    K_int j = 2*INT_PTR(x)[0];
    FOR_EACH(x) j -= INT_PTR(x)[i];
    return j;
}

// */x
K_int mulInts(K x){
    K_int j = 1;
    FOR_EACH(x) j *= INT_PTR(x)[i];
    return j;
}

K over1Int(K f, K x){
    return UNREF_X(kint(PICK3(TAG_VAL(f)-1, sumInts, subInts, mulInts)(x)));
}

// general cases

// f/x
K over1Generic(K f, K x){
    if (HDR_COUNT(x) == 0) return x;
    K r;
    FOR_EACH(x){
        r = !i ? item(i, x) : apply(f, 2, (K[]){r, item(i, x)});
        if (!r){ unref(x); return r; }
    }
    return UNREF_X(r);
}

K over2(K f, K x, K y){
    (void)f;
    NYI_ERROR(1, "over2", UNREF_XY(0));
}

// scan (accumulate)

K scan1(K f, K x){
    return TAG_TYPE(f)==KOpType && TAG_VAL(f)==1 && HDR_TYPE(x)==KBoolType ? sumsBools(x) : scan1Generic(f, x);
}

// specialized kernels

K sumsBools(K x){
    K r = knew(KIntType, HDR_COUNT(x));
    K_int *ints = INT_PTR(r);
    FOR_EACH(x) ints[i] = !i ? GET_BIT(x,0) : ints[i-1] + GET_BIT(x,i);
    return UNREF_X(r);
}

// general cases

// f\x
K scan1Generic(K f, K x){
    K t, r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        t = !i ? item(i, x) : apply(f, 2, (K[]){ref(t), item(i, x)});
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X(squeeze(r));
}

K scan2(K f, K x, K y){
    (void)f;
    NYI_ERROR(1, "scan2", UNREF_XY(0));
}

// prior (pairwise)

// f':x. x[0] passes through, so -': is deltas
K prior1(K f, K x){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = !i ? item(i, x) : apply(f, 2, (K[]){item(i, x), item(i-1, x)});
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X( squeeze(r) );
}

K prior2(K f, K x, K y){
    (void)f;
    NYI_ERROR(1, "prior2", UNREF_XY(0));
}
