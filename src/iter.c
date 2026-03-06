// iterators and acculators

#include "iter.h"

// f'[x;y] for binary operators
// consume x y
K each2binary(DYAD f, K x, K y){
    K r = knew(KObjType, HDR_COUNT(x)), *robj = OBJ_PTR(r);
    FOR_EACH(r){
        K t = f(item(i, x), item(i, y));
        if (!t){ HDR_COUNT(r)=i; unref(r); return UNREF_XY(0); }
        robj[i] = t;
    }
    return UNREF_XY(r);
}