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
    K r = vm(OBJ_PTR(x)[0], OBJ_PTR(x)[1], OBJ_PTR(x)[2], GLOBALS, vn, locals);
    while (vn--) unref(locals[vn]);
    return r;
}

K applyOver(K x, int n, K *args){
    if (n == 1) return index(x, *args);
    for (int i = 0; i < n; i++){
        K t = apply(x, 1, args + i); // don't overwrite x yet...
        if (i) unref(x); // because we need to unref intermediates which are not the first
        if (!t) { while (++i < n) unref(args[i]); return 0; }
        x = t;
    }
    return x;
}

// generic apply
K apply(K x, int n, K *args){
    RANK_ERROR(IS_TAG(x), "can't apply atom to argument", while(n--) unref(args[n]));
    K r = (HDR_TYPE(x) == KLambdaType ? applyLambda : applyOver)(x, n, args);
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