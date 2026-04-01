#include "file.h"

// forward declaration
K eval(K);

// read a file into a K_char list
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

// read lines from a file
static K readLines(K path){
    K f = readFile(path);
    return f ? cutStr(f, '\n') : 0;
}

// execute a .k file
K kfile(K path){
    // sanity checks and file read
    TYPE_ERROR(IS_TAG(path) || HDR_TYPE(path) != KChrType, "'\\l filepath' expects KChrType list", unref(path));
    VALUE_ERROR(
        HDR_COUNT(path) < 3 || (CHR_PTR(path)[HDR_COUNT(path)-2] != '.' || CHR_PTR(path)[HDR_COUNT(path)-1] != 'k'),
        "'\\l filepath' expects filepath like 'file.k'",
        path,
        unref(path)
    );
    K r = 0, line = readLines(path);
    if (!line) return 0;
    // evaluate line one by one
    FOR_EACH(line){
        // skip empty
        if (HDR_COUNT(OBJ_PTR(line)[i]) == 0) continue;
        // keeps last r. unref(0) is safe
        unref(r);
        r = eval(ref(OBJ_PTR(line)[i]));
        if (!r) { unref(line); return 0; };
    }
    unref(line);
    return r;
}