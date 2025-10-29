#include "apply.h"
#include "object.h"

// index a simple list
K simpleIndex(K r, K x, K_int* ix){
    size_t m = HDR_COUNT(x);
    switch(WIDTH_OF(x)){
    case 1: {K_char *d = CHR_PTR(r), *s = CHR_PTR(x); FOR_EACH(r) d[i] = ix[i] < m ? s[ix[i]] :' '; break;}
    case 4: {K_int  *d = INT_PTR(r), *s = INT_PTR(x); FOR_EACH(r) d[i] = ix[i] < m ? s[ix[i]] :  0; break;}
    case 8: {K_long *d = LNG_PTR(r), *s = LNG_PTR(x); FOR_EACH(r) d[i] = ix[i] < m ? s[ix[i]] :  0; break;}
    }
    return r;
}

K index(K x, K ix){
    return 
        TAG_TYPE(ix) ? 
        TAG(HDR_TYPE(x), TAG_VAL(ix)<(size_t)HDR_COUNT(x) ? (HDR_TYPE(x)==KChrType ? CHR_PTR(x)[TAG_VAL(ix)] : INT_PTR(x)[TAG_VAL(ix)]) : "\0 "[HDR_TYPE(x)==KChrType]):
        simpleIndex(knew(HDR_TYPE(x), HDR_COUNT(ix)), x, INT_PTR(ix));
}

// generic apply
K apply(K x, int n, K*args){
    K ix = *args;
    NYI_ERROR(
        n != 1 || TAG_TYPE(x) || (HDR_TYPE(x)!=KChrType&&HDR_TYPE(x)!=KIntType) || KIntType!=(TAG_TYPE(ix)?TAG_TYPE(ix):HDR_TYPE(ix)), 
        "apply. only basic functionality",
        unref(x); while(n--) unref(args[n])
    );
    return index(x, ix);
}
