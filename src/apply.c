#include "apply.h"
#include "object.h"
#include "eval.h"

// forward declarations
K index(K, K);
K apply(K, int, K*);

// application

K applyLambda(K x, int n, K *args){
    RANK_ERROR(HDR_ARGC(x) < n, "too many lambda args", while(n--) unref(args[n]));
    // NYI error is fine, as argc < n is allowed (not an error, returns projection) but not supported yet
    NYI_ERROR(HDR_ARGC(x) > n, "lambda projection", while(n--) unref(args[n]));
    int vn = HDR_VARC(x); // total var count = argc + localc (excl. globalc)
    K locals[vn];
    for (int i = 0; i < vn; i++)
        locals[i] = i < HDR_ARGC(x) ? args[i] : 0;
    K r = vm(OBJ_PTR(x)[0], OBJ_PTR(x)[1], OBJ_PTR(x)[2], vn, locals);
    while (vn--) unref(locals[vn]);
    return r;
}

K each1(K f, K x){
    RANK_ERROR(IS_ATOM(x), "f'atom", unref(x));
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = item(i, x);
        t = apply(f, 1, &t);
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X( squeeze(r) );
}

K applyOperator(K x, int n, K *args){
    NYI_ERROR(n > 2, "applyOperator n>2", while(n--) unref(args[n]));
    return n == 1 ? unary_op[TAG_VAL(x)](*args) : binary_op[TAG_VAL(x)](*args, args[1]);
}

K applyAdverb(K x, int n, K *args){
    NYI_ERROR(n != 1 || HDR_ADVERB(x) != 0, "adverb", while(n--) unref(args[n]));
    return each1(OBJ_PTR(x)[0], args[0]);
}

K applyOver(K x, int n, K *args){
    if (n == 1) return index(x, *args);
    ref(x);
    for (int i = 0; i < n; i++){
        K t = apply(x, 1, args + i);
        unref(x);
        if (!t) { while (++i < n) unref(args[i]); return 0; }
        x = t;
    }
    return x;
}

// generic apply
// borrows argument x, should not housekeep
K apply(K x, int n, K *args){
    if (TAG_TYPE(x) == KOpType) return applyOperator(x, n, args);
    RANK_ERROR(IS_TAG(x), "can't apply atom to argument", while(n--) unref(args[n]));
    K r = (HDR_TYPE(x) == KLambdaType ? applyLambda : HDR_TYPE(x) == KAdverbType ? applyAdverb : applyOver)(x, n, args);
    return r;
}

// indexing

// index a list with a list
static K listIndex(K r, K x, K_int *ix){
    K_int m = HDR_COUNT(x);
    switch(WIDTH_OF(x)){
    case 1: {K_char *d = CHR_PTR(r), *s = CHR_PTR(x); FOR_EACH(r) d[i] = OOB(ix[i], m) ?' ': s[ix[i]]; break;}
    case 4: {K_int  *d = INT_PTR(r), *s = INT_PTR(x); FOR_EACH(r) d[i] = OOB(ix[i], m) ? 0 : s[ix[i]]; break;}
    case 8: {K_long *d = LNG_PTR(r), *s = LNG_PTR(x); FOR_EACH(r) d[i] = OOB(ix[i], m) ? 0 : s[ix[i]]; break;}
    }
    if (HDR_TYPE(r) == KObjType) FOR_EACH(r) ref(OBJ_PTR(r)[i]);
    return r;
}

// index a list with an atom
static K atomIndex(K x, K_int i){
    K_int t = HDR_TYPE(x);
    return t ? TAG(t, OOB(i,HDR_COUNT(x)) ? "\0 "[t==KChrType] : t==KIntType ? INT_PTR(x)[i] : CHR_PTR(x)[i]) : ref(OBJ_PTR(x)[i]);
}

K index(K x, K ix){
    K r = TAG_TYPE(ix) ? atomIndex(x, TAG_VAL(ix)) : listIndex(knew(HDR_TYPE(x), HDR_COUNT(ix)), x, INT_PTR(ix));
    unref(ix);
    return r;
}