#ifndef REFCOUNT_H
#define REFCOUNT_H

#ifdef TRACK_REFS

// Function declarations
K track_knew(K_char t, K_int n, const char *file, int line);
void track_unref(K x, const char *file, int line);
void reset(void);
int check_leaks(K globals);
void mark(K x);

// Intercept allocations and deallocations
#define knew(t,n) track_knew(t, n, __FILE__, __LINE__)
#define unref(x) track_unref(x, __FILE__, __LINE__)

// Functions that need direct access to real functions
extern K _knew(K_char t, K_int n);
extern void _unref(K x);

#endif // TRACK_REFS
#endif // REFCOUNT_H