#include "op_unary.h"

// iteration helpers over unary operators

K _each1(F1 f, K x){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = f(item(i, x));
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X(r);
}


static K nyi1(K x){NYI_ERROR(1, "unary operator", unref(x);)}

//               :     +     -    *     %     &      |     <     >     =     @     .      !     ,     ?     #      _     ~     $     ^
F1 unary_op[] = {nyi1, nyi1, neg, nyi1, nyi1, where, nyi1, nyi1, nyi1, nyi1, nyi1, value, til, nyi1, nyi1, count, nyi1, nyi1, nyi1, nyi1};

// -x
K neg(K x){
    if (IS_TAG(x)){
        TYPE_ERROR(TAG_TYPE(x) != KIntType, "-x must be int", );
        return TAG(KIntType, -TAG_VAL(x));
    } else if (HDR_TYPE(x) == KObjType){
        return _each1(neg, x);
    } else if (HDR_TYPE(x) == KIntType){
        K r = knew(KIntType, HDR_COUNT(x));
        FOR_EACH(x) INT_PTR(r)[i] = -INT_PTR(x)[i];
        return UNREF_X(r);
    }
    TYPE_ERROR(1, "-x must be int", unref(x));
}

// &x
K where(K x){
    TYPE_ERROR(IS_TAG(x) || HDR_TYPE(x) != KBoolType, "&x must be bool", unref(x));
    // over-read in both loops depends on zeroed last word beyond logical length n
    K_int n = 0, m = (HDR_COUNT(x)+7)/8;
    for (K_int i = 0; i < m; i++){
        n += stdc_count_ones(((uint64_t*)x)[i]);
    }
    K r = knew(KIntType, n);
    K_int idx = 0;
    for (K_int i = 0, o = 0; i < m; i++, o += 8){
        uint64_t word = ((uint64_t*)x)[i];
        // special-case packed word
        if (word == 0x0101010101010101UL){
            for (int j = 0; j < 8; j++) INT_PTR(r)[idx++] = o+j;
            continue;
        }
        while (word){
            INT_PTR(r)[idx++] = o + stdc_trailing_zeros(word)/8;
            word &= word - 1;
        }
    }
    return UNREF_X(r);
}

K readFile(K path) {
    path = joinTag(path, 0);
    FILE *f = fopen((char*)CHR_PTR(path), "rb");
    VALUE_ERROR(!f, "can't open file: ", path, unref(path));
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    K r = knew(KChrType, size);
    fread(CHR_PTR(r), 1, size, f);
    fclose(f);
    
    unref(path);
    return r;
}

// .x
K value(K x){
    TYPE_ERROR(TAG_TYPE(x) || HDR_TYPE(x) != KChrType, ". x", unref(x));
    return readFile(x);
}

// !x
K til(K x){
    TYPE_ERROR(TAG_TYPE(x) != KIntType, "!x must provide int atom", unref(x));
    K r = knew(KIntType, TAG_VAL(x));
    FOR_EACH(r) INT_PTR(r)[i] = i;
    return r;
}

// #x
K count(K x){
    return UNREF_X(TAG(KIntType, IS_ATOM(x) ? 1 : HDR_COUNT(x)));
}
