#include "apply.h"
#include "object.h"

K simpleIndex(K r, K x, K_int* ix){
    size_t m = HDR_COUNT(x);
    switch(WIDTH_OF(x)){
    case 1: {K_char *d = CHR_PTR(r), *s = CHR_PTR(x); FOR_EACH(r) d[i] = ix[i] < m ? s[ix[i]] : 0; break;}
    case 4: {K_int  *d = INT_PTR(r), *s = INT_PTR(x); FOR_EACH(r) d[i] = ix[i] < m ? s[ix[i]] : 0; break;}
    }
    return r;
}

K index(K x, K ix){
    return TAG_TYPE(ix) ? TAG(HDR_TYPE(x), TAG_VAL(ix)<(size_t)HDR_COUNT(x) ? (HDR_TYPE(x)==KChrType ? CHR_PTR(x)[TAG_VAL(ix)] : INT_PTR(x)[TAG_VAL(ix)]) : 0): simpleIndex(knew(HDR_TYPE(x), HDR_COUNT(ix)), x, INT_PTR(ix));
}

// generic apply
K apply(K x, int n, K*args){
    NYI_ERROR(n != 1, "apply n>1", unref(x); while(n--) unref(args[n]));
    K ix = *args;
    NYI_ERROR(TAG_TYPE(x) || (HDR_TYPE(x)!=KChrType&&HDR_TYPE(x)!=KIntType) || KIntType!=(TAG_TYPE(ix)?TAG_TYPE(ix):HDR_TYPE(ix)), "apply", unref(x); while(n--) unref(args[n]));
    return index(x, ix);
}
