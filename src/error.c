// error handling implementation

#include "error.h"

// Error state variables (defined here, declared extern in error.h)
int kerrno = -1;  // -1 = uninitialized; errors start at 0 (KERR_PARSE)
int kerrpos = -1;
const char *kerrstr = "";
char kerrbuf[LINE_LEN * 2];

const char *kerr_names[] = {
    "parse",
    "type",
    "length",
    "value",
    "rank",
    "nyi",
};

void kperror(char *src){
    fprintf(stderr, "'%s! %s %s\n", kerr_names[kerrno], kerrstr, kerrno == KERR_VALUE ? kerrbuf : "");
    if (kerrno == KERR_PARSE) {
        fprintf(stderr, "    %s\n%*s^\n", src, kerrpos+4, "");
    }
}

