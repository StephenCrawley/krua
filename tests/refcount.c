#ifdef TRACK_REFS

#include "krua.h"
#include "object.h"

#define MAX_TRACKED 1024

static struct {
    K obj[MAX_TRACKED];
    bool alive[MAX_TRACKED];
    bool marked[MAX_TRACKED];
    const char *alloc_file[MAX_TRACKED];
    int alloc_line[MAX_TRACKED];
    const char *free_file[MAX_TRACKED];
    int free_line[MAX_TRACKED];
    int count;
} tracker = {0};

K track_knew(K_char t, K_int n, const char *file, int line) {
    K x = _knew(t, n);
    
    if (tracker.count < MAX_TRACKED) {
        int i = tracker.count++;
        tracker.obj[i] = x;
        tracker.alive[i] = true;
        tracker.marked[i] = false;
        tracker.alloc_file[i] = file;
        tracker.alloc_line[i] = line;
        tracker.free_file[i] = NULL;
        tracker.free_line[i] = 0;
        
        #ifdef DEBUG_TRACKER
        printf("ALLOC #%d: %p at %s:%d\n", i, (void*)x, file, line);
        #endif
    }
    
    return x;
}

void track_unref(K x, const char *file, int line) {
    if (!x || IS_TAG(x)) return;
    
    // Check tracker first (search backward for most recent)
    for (int i = tracker.count - 1; i >= 0; i--) {
        if (tracker.obj[i] == x) {
            if (!tracker.alive[i]) {
                fprintf(stderr, 
                    "ERROR: use-after-free at %s:%d\n"
                    "  object #%d was allocated at %s:%d\n"
                    "  but already freed at %s:%d\n",
                    file, line, i,
                    tracker.alloc_file[i], tracker.alloc_line[i],
                    tracker.free_file[i], tracker.free_line[i]);
                abort();
            }
            
            // Alive - check if dying or just decrementing
            if (HDR_REFC(x) > 0) {
                _unref(x);
                return;
            }
            
            // Dying - mark dead and record
            tracker.alive[i] = false;
            tracker.free_file[i] = file;
            tracker.free_line[i] = line;
            
            #ifdef DEBUG_TRACKER
            printf("FREE #%d: %p at %s:%d (allocated at %s:%d)\n", 
                   i, (void*)x, file, line, 
                   tracker.alloc_file[i], tracker.alloc_line[i]);
            #endif
            
            _unref(x);
            return;
        }
    }
    
    // Untracked object
    #ifdef DEBUG_TRACKER
    printf("WARNING: unref on untracked object at %s:%d\n", file, line);
    #endif
    _unref(x);
}

void mark(K x) {
    if (!x || IS_TAG(x)) return;
    
    // Find in tracker (search backward for most recent)
    for (int i = tracker.count - 1; i >= 0; i--) {
        if (tracker.obj[i] == x && tracker.alive[i]) {
            if (tracker.marked[i]) return;  // Already marked
            tracker.marked[i] = true;
            
            // Recursively mark children
            if (HDR_TYPE(x) == KObjType) {
                FOR_EACH(x) mark(OBJ_PTR(x)[i]);
            }
            return;
        }
    }
}

int check_leaks(K globals) {
    // Clear marks
    for (int i = 0; i < tracker.count; i++) {
        tracker.marked[i] = false;
    }
    
    // Mark from globals (recursively marks all reachable)
    mark(globals);

    // Count leaks
    int leaks = 0;
    for (int i = 0; i < tracker.count; i++) {
        if (tracker.alive[i] && !tracker.marked[i]) {
            printf("LEAK #%d: allocated at %s:%d\n", 
                   i, tracker.alloc_file[i], tracker.alloc_line[i]); 
            leaks++;
        }
    }
    
    return leaks;
}

void reset(void) {
    tracker.count = 0;
}

#endif // TRACK_REFS