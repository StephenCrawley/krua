#include "op_unary.h"

static K nyi1(K x){NYI_ERROR(1, "unary operator", unref(x);)}

F1 unary_op[] = {nyi1, nyi1, neg, nyi1, nyi1, nyi1, value, nyi1, nyi1, nyi1, nyi1, nyi1, count, nyi1, nyi1, nyi1, nyi1, nyi1, nyi1, nyi1};

K neg(K x){
    TYPE_ERROR(KIntType != (TAG_TYPE(x) ? TAG_TYPE(x) : HDR_TYPE(x)), "-x must be int", unref(x));
    if (TAG_TYPE(x)) return TAG(KIntType, -TAG_VAL(x));
    K r = knew(KIntType, HDR_COUNT(x));
    FOR_EACH(x) INT_PTR(r)[i] = -INT_PTR(x)[i];
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

K value(K x){
    TYPE_ERROR(TAG_TYPE(x) || HDR_TYPE(x) != KChrType, ". x", unref(x));
    return readFile(x);
}

K count(K x){
    return UNREF_X(TAG(KIntType, IS_ATOM(x) ? 1 : HDR_COUNT(x)));
}