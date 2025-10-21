// error handling

#ifndef ERROR_H
#define ERROR_H

#include "krua.h"

// Error state (errno-style)
extern int kerrno;
extern int kerrpos;
extern const char *kerrstr;
extern char kerrbuf[512];
extern const char *kerr_names[];
void kperror(char *src);

enum {
    KERR_PARSE,        // Parse error
    KERR_TYPE,         // Type error
    KERR_LENGTH,       // Length error
    KERR_NYI,          // Not yet implemented
    KERR_VALUE,        // Value error (undefined var, etc)
};    

#define PARSE_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_PARSE; kerrpos = i; kerrstr = e; cleanup; return 0; }
#define TYPE_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_TYPE; kerrstr = e; cleanup; return 0; }
#define NYI_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_NYI; kerrstr = e; cleanup; return 0; }
#define VALUE_ERROR(p, e, v, cleanup) \
    if (p){ kerrno = KERR_VALUE; kerrstr = e; MEMCPY(kerrbuf, &v, sizeof(K_sym)); kerrbuf[sizeof(K_sym)]=0; cleanup; return 0; }

#endif