#include "monad.h"

static K nyi1(K x){NYI_ERROR(1, "monadic operation", unref(x);)}

MONAD monad_table[] = {nyi1, nyi1, nyi1, nyi1, nyi1, nyi1, value, nyi1, nyi1, nyi1, nyi1, nyi1, count, nyi1, nyi1, nyi1, nyi1, nyi1, nyi1, nyi1};

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
    NYI_ERROR(TAG_TYPE(x) || HDR_TYPE(x) != KChrType, "value", unref(x));
    return readFile(x);
}

K count(K x){
    return UNREF_X(TAG(KIntType, IS_ATOM(x) ? 1 : HDR_COUNT(x)));
}