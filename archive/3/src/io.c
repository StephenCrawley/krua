#include "io.h"
#include "object.h"

#define MAX_LINE_LENGTH 512

// x is a KC object containing filename
// reads file lines into a list of KC objects
K readLines(K x){
    // extract filename
    i64 xn=HDR_CNT(x);
    char f[xn+1];
    memcpy(f,CHR(x),xn);
    f[xn]=0;

    // open filehandle
    FILE *file=fopen(f,"r");
    if (!file) return UNREF_X(kerr(f));

    // count lines
    i64 rn=0;
    char buffer[MAX_LINE_LENGTH];
    while (fgets(buffer, sizeof(buffer), file) != NULL) 
        rn++;
    fseek(file, 0, SEEK_SET);

    // return object
    K r=tn(KK,0);

    // read each line and append to return object
    for (i64 i=0; i<rn; i++) {
        if (fgets(buffer, sizeof(buffer), file) == NULL) {
            fclose(file);
            return UNREF_XR(kerr(f));
        }

        // Remove newline character if present
        size_t len=strlen(buffer);
        if (len>0 && buffer[len-1] == '\n') {
            buffer[len-1] = 0;
        }

        // append new KC object
        r=jk(r,kC0(buffer));
    }

    fclose(file);
    return UNREF_X(r);
}
