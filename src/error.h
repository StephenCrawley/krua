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
extern void _copy_sym(), _copy_chr(); // used in VALUE_ERROR

enum {
    KERR_PARSE,        // Parse error
    KERR_TYPE,         // Type error
    KERR_LENGTH,       // Length error
    KERR_VALUE,        // Value error (undefined var, etc)
    KERR_RANK,         // Rank error (too many )
    KERR_NYI,          // Not yet implemented
};

#define PARSE_ERROR(p, pos, e, cleanup) \
    if (p){ kerrno = KERR_PARSE; kerrpos = (pos); kerrstr = (e); cleanup; return 0; }
#define TYPE_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_TYPE; kerrstr = e; cleanup; return 0; }
#define VALUE_ERROR(p, e, v, cleanup) \
    if (p){ kerrno = KERR_VALUE; kerrstr = e; _Generic((v), K_sym: _copy_sym, K: _copy_chr)(v); cleanup; return 0; }
#define RANK_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_RANK; kerrstr = e; cleanup; return 0; }
#define NYI_ERROR(p, e, cleanup) \
    if (p){ kerrno = KERR_NYI; kerrstr = e; cleanup; return 0; }

#endif