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
    if (kerrno == KERR_PARSE && kerrpos != -1) {
        fprintf(stderr, "    %s\n%*s^\n", src, kerrpos+4, "");
    }
}

// functions to copy string into kerrbuf in VALUE_ERROR
inline void _copy_sym(K_sym v) {
    MEMCPY(kerrbuf, &v, sizeof(K_sym));
    kerrbuf[sizeof(K_sym)] = 0;
}
inline void _copy_chr(K x) {
    K_int n = HDR_COUNT(x) < sizeof(kerrbuf)-1 ? HDR_COUNT(x) : sizeof(kerrbuf)-1;
    MEMCPY(kerrbuf, x, n);
    kerrbuf[n] = 0;
}
