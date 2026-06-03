#include "adverb.h"
#include "object.h"
#include "apply.h"
#include "op_unary.h"
#include "error.h"

// forward declarations
K each1(K, K);
K over1(K, K);
K scan1(K, K);
K sumOver(K);
K subOver(K);
K mulOver(K);
K each1Generic(K, K);
K over1Generic(K, K);
K over1Special(K, K);
K scan1Generic(K, K);
//K each2(K, K, K);
//K over2(K, K, K);
//K scan2(K, K, K);

// dispatch

// f'x f/x f\x
K adv1(K f, K x){
    RANK_ERROR(IS_ATOM(x), "f'atom", unref(x));
    NYI_ERROR(HDR_TYPE(x) == KBoolType, "adverb on bool", unref(x));
    return PICK3(HDR_ADVERB(f), each1, over1, scan1)(OBJ_PTR(f)[0], x);
}
K each1(K f, K x){
    return TAG_TYPE(f) == KOpType && TAG_VAL(f) == 2 ? neg(x) : each1Generic(f, x);
}

K over1(K f, K x){
    return (TAG_TYPE(f) != KOpType || OOB(TAG_VAL(f)-1, 3) ? over1Generic : over1Special)(f, x);
}

K scan1(K f, K x){
    return scan1Generic(f, x);
}

/*
// f'[x;y] f/[x;y] f\[x;y]
K adv2(K f, K x, K y){
    // 0=each 1=over 2=scan
    K_int a = HDR_ADVERB(f);
    return (a == 0 ? each2 : a == 1 ? over2 : scan2)(OBJ_PTR(f)[0], x, y);
}
*/

// each (map)

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

// over (reduce)

// specialized kernels

// +/x
K sumOver(K x){
    K_int j = 0;
    FOR_EACH(x) j += INT_PTR(x)[i];
    return kint(j);
}

// -/x
K subOver(K x){
    if (!HDR_COUNT(x)) return kint(0);
    K_int j = 2*INT_PTR(x)[0];
    FOR_EACH(x) j -= INT_PTR(x)[i];
    return kint(j);
}

// */x
K mulOver(K x){
    K_int j = 1;
    FOR_EACH(x) j *= INT_PTR(x)[i];
    return kint(j);
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

K over1Special(K f, K x){
    return HDR_TYPE(x) == KIntType ? UNREF_X(PICK3(TAG_VAL(f)-1,sumOver,subOver,mulOver)(x)) : over1Generic(f, x);
}

// scan (accumulate)

// TODO: specialized kernels

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