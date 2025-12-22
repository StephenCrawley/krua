// K object create/destroy/print

#include "object.h"

#define MIN_ALLOC 32UL  // minimum bytes per object
#define BUCKET_SHIFT 6  // log2(MIN_ALLOC)
#define BUCKET_SIZEOF(x) (MIN_ALLOC << HDR_BUCKET(x))  // size of the bucket that x is in
#define NUM_BUCKETS 25
#define HEAP_SIZE   (1ULL << 29) // 512MiB
K M[NUM_BUCKETS];   // list of linked lists which are free to use

// ** K object reference ** //


// increment refcount
K ref(K x){
    if (!IS_TAG(x)) HDR_REFC(x)++;
    return x;
}

// decrement refcount
// internal function, hidden behind `unref`, which may wrap refcount tracking if enabled
void _unref(K x){
    if (!x || IS_TAG(x) || HDR_REFC(x)--){
        return;
    }
    if (IS_NESTED(x)){
        FOR_EACH(x){ unref(OBJ_PTR(x)[i]); }
    }
    M[HDR_BUCKET(x)] = OBJ_PTR(x)[0] = M[HDR_BUCKET(x)];
}

// ** K object allocate and memcpy ** //

// list creation

// buddy alloc an object
K kalloc(K_int n){
    K x, r;

    // minimum allocation is 32 bytes. subtract from 59 so bucket 0 contains 32 bytes 
    K_int b, bucket = (64 - BUCKET_SHIFT) - __builtin_clzll(n + MIN_ALLOC - 1);
    b = bucket;

    // if there's already a free bucket in the list, return it 
    x = M[bucket];
    if (x){
        M[bucket] = OBJ_PTR(x)[0]; // place next free bucket (could be 0) in the free-bucket list
        return x;
    }
    // else find the next largest bucket and divide it into smaller blocks

    // find the next largest bucket
    while (!M[++b]){
        // currently max heapsize is 512MiB. given bucket 0 is 32 bytes, there are only 26 bucket sizes
        if (b == NUM_BUCKETS-1){
            K_hdr *new_alloc = malloc(HEAP_SIZE); // 512MiB
            if (!new_alloc) { fprintf(stderr, "Out of memory\n"); exit(1); }
            M[b] = (K)(1 + new_alloc); // skip the header and place a pointer to the array in the bucket
            break;
        }
    }

    // now split the bucket
    r = x = M[b];
    HDR_BUCKET(x) = bucket; // set the bucket in our new allocation
    M[b] = OBJ_PTR(x)[0]; // stick next object in linked list in free list
    while (bucket < b){ // now split the buckets and put them in the list
        x += MIN_ALLOC << HDR_BUCKET(x);
        HDR_BUCKET(x) = bucket;
        M[bucket++] = x;
        OBJ_PTR(x)[0] = 0;
    }

    return r;
}

// allocate a new list
// internal function, hidden behind `knew`, which may wrap in refcount tracking if enabled
K _knew(K_char t, K_int n){
    K x = (K)kalloc(sizeof(K_hdr) + n*KWIDTHS[t]);
    HDR_TYPE(x) = t;
    HDR_REFC(x) = 0;
    HDR_COUNT(x) = n;
    return x;
}

// box x
K k1(K x){
    K r = knew(KObjType, 1);
    OBJ_PTR(r)[0] = x;
    return r;
}

// (x;y)
K k2(K x, K y){
    K r = knew(KObjType, 2);
    OBJ_PTR(r)[0] = x;
    OBJ_PTR(r)[1] = y;
    return r;
}

// (x;y;z)
K k3(K x, K y, K z){
    K r = knew(KObjType, 3);
    OBJ_PTR(r)[0] = x;
    OBJ_PTR(r)[1] = y;
    OBJ_PTR(r)[2] = z;
    return r;
}

// (x;y;z;w)
K k4(K x, K y, K z, K w){
    K r = knew(KObjType, 4);
    OBJ_PTR(r)[0] = x;
    OBJ_PTR(r)[1] = y;
    OBJ_PTR(r)[2] = z;
    OBJ_PTR(r)[3] = w;
    return r;
}

// K_chr list from nul-terminated c-string
K kcstr(const char *s){
    return knewcopy(KChrType, strlen(s), (K)s);
}

K kc1(K_char a){
    K r = knew(KChrType, 1);
    CHR_PTR(r)[0] = a;
    return r;
}

K kc2(K_char a, K_char b){
    K r = knew(KChrType, 2);
    CHR_PTR(r)[0] = a;
    CHR_PTR(r)[1] = b;
    return r;
}

K ksymdict(){
    return k2(knew(KSymType,0), knew(KObjType,0));
}

// atom creation

// create K_char atom
K_sym encodeSym(K_char *src, int n){
    // TODO: proper sym iterning
    K_int sym = 0;
    memcpy(&sym, src, n > 4 ? 4 : n);
    return sym;
}

K_char addSym(K *syms, K_sym x){
    // first var name encountered
    if (*syms == 0){
        *syms = knewcopy(KSymType, 1, (K)&x);
        return 0;
    }
    // check if var name already in list
    K_int i = -1;
    K_sym *s = SYM_PTR(*syms);
    while (++i < HDR_COUNT(*syms)){ if (x == s[i]) return i; }
    // else join to list
    *syms = joinTag(*syms, x);
    return i;
}

K* getSlot(K dict, K_sym key){
    // add the key
    K_int i = addSym(&OBJ_PTR(dict)[0], key);
    // init the value if it doesn't exist
    if (i == HDR_COUNT(OBJ_PTR(dict)[1])){
        OBJ_PTR(dict)[1] = joinTag(OBJ_PTR(dict)[1], 0);
    }
    return OBJ_PTR(OBJ_PTR(dict)[1]) + i;
}

K_int findSym(K x, K_sym y){
    K_sym *v = SYM_PTR(x);
    FOR_EACH(x) { if(v[i] == y) return i; }
    return HDR_COUNT(x);
}

// utility functions (copy)

// allocate a new list and copy n items from x
K knewcopy(K_char t, K_int n, K x){
    K r = MEMCPY(knew(t,n), x, n*KWIDTHS[t]);
    if (HDR_TYPE(r) == KObjType){ // TODO? handle all types which are list of K objects
        FOR_EACH(r) { ref(OBJ_PTR(r)[i]); }
    }
    return r;
}

// append y to x. like a memcpy wrapper but handles KObjType y and refs as needed
K kcpy(K x, K y){
    if (HDR_TYPE(y) == KObjType){ // TODO? handle all types which are list of K objects
        FOR_EACH(y) { OBJ_PTR(x)[i] = ref(OBJ_PTR(y)[i]); }
    } else {
        MEMCPY(x, y, HDR_COUNT(y)*WIDTH_OF(y));
    }
    return x;
}

// increase the count of list x by n items. reuse x if possible
static K kextend(K x, K_int n){
    n += HDR_COUNT(x);
    if (HDR_REFC(x) || BUCKET_SIZEOF(x) < (n * WIDTH_OF(x) + sizeof(K_hdr))){
        return UNREF_X(kcpy(knew(HDR_TYPE(x), n), x));
    }
    HDR_COUNT(x) = n;
    return x;
}

// cutStr("ab,cd", ',') -> ("ab";"cd")
K cutStr(K x, K_char c){
    K_int n = 1;
    FOR_EACH(x) if(CHR_PTR(x)[i] == c) n++;
    K r = knew(KObjType, n);
    K_char *s=CHR_PTR(x), *e=s, *end=s+HDR_COUNT(x);
    for (int i=0; i<n; i++){
        while (e<end && *e!=c) e++;
        OBJ_PTR(r)[i] = knewcopy(KChrType, e-s, (K)s);
        s = ++e;
    }
    return UNREF_X(r);
}

// joinStr(("ab";"cd"), '|') -> "ab|cd"
// special case: joinStr(("ab";"cd"), 0) -> "abcd"
K joinStr(K x, K_char c){
    K_int n = c ? HDR_COUNT(x) : 0;
    FOR_EACH(x) n += HDR_COUNT(OBJ_PTR(x)[i]);
    K r = knew(KChrType, n);
    K_char *s = CHR_PTR(r);
    FOR_EACH(x){ s = HDR_COUNT(OBJ_PTR(x)[i]) + (K_char*)kcpy((K)s, OBJ_PTR(x)[i]); if (c)*s++ = c; }
    if (c) HDR_COUNT(r)--;
    return UNREF_X(r);
}

// ** K object operations ** //

// join tagged K to list
K joinTag(K x, K y){
    x = kextend(x, 1);
    MEMCPY(PTR_TO(x, HDR_COUNT(x)-1), &y, WIDTH_OF(x));
    return x;
}

K joinObj(K x, K y){
    x = kextend(x, 1);
    OBJ_PTR(x)[HDR_COUNT(x)-1] = y;
    return x;
}

// (1;2) -> 1 2
K squeeze(K x){
    if (!x || HDR_COUNT(x) == 0) return x;
    K_char type = TAG_TYPE(OBJ_PTR(x)[0]);
    if (!type) return x;
    FOR_EACH(x) if (type != TAG_TYPE(OBJ_PTR(x)[i])) return x;
    K r = knew(type, HDR_COUNT(x));
    switch(WIDTH_OF(r)){
    case 1: {K_char *d = CHR_PTR(r); FOR_EACH(r) d[i] = TAG_VAL(OBJ_PTR(x)[i]); break;}
    case 4: {K_int  *d = INT_PTR(r); FOR_EACH(r) d[i] = TAG_VAL(OBJ_PTR(x)[i]); break;}
    }
    return UNREF_X(r);
}

// ** K object print ** //

// recursively print a K object x
// internal function, hidden behind `kprint`
// TODO: replace with custom buffered write which can be leveraged by $ (string) and other primitives
static void _kprint(K x){
    if (!x) return;
    K_char type;

    if (IS_TAG(x)){
        type = TAG_TYPE(x);
        if (type == KChrType){
            printf("\"%c\"", TAG_VAL(x));
        } else if (type == KIntType){
            printf("%d", TAG_VAL(x));
        } else if (type == KSymType){
            long long s=(long long)TAG_VAL(x);
            printf("`%.4s", (char*)&s);
        } else if (type == KMonadType) {
            return;
        } else {
            printf("'print not supported for type: %d", type);
            exit(1);
        }
        return;
    }

    K_int n = HDR_COUNT(x);

    if (n == 0){
        char *empty[] = {"()", "\"\"", "0#0"};
        printf("%s", empty[HDR_TYPE(x)]);
        return;
    }
    
    if (n == 1) putchar(',');

    type = HDR_TYPE(x);
    if (type == KObjType){
        if (n != 1) putchar('(');
        FOR_EACH(x){ 
            if (i > 0) putchar(';');
            _kprint(OBJ_PTR(x)[i]);
        }
        if (n != 1) putchar(')');
    } else if (type == KChrType){
        printf("\"%.*s\"", n, (K_char*)x);
    } else if (type == KIntType) {
        FOR_EACH(x) { printf("%d ", INT_PTR(x)[i]); }
    } else if (type == KSymType){
        FOR_EACH(x){
            K_char *s = CHR_PTR(&SYM_PTR(x)[i]);
            int j=0; putchar('`'); while (j++<4 && *s) putchar(*s++);
        }
    } else if (type == KLambdaType) {
        _kprint(OBJ_PTR(x)[n - 1]); // last object in KLambdaType is a K string of the lambda
    } else { // TODO: remove
        printf("print not supported for type: %d", type);
        exit(1);
    }
}

// write K object to stdout. public wrapper around recursive outr()
K kprint(K x){
    if (x == knull()) return x;
    _kprint(x);
    putchar('\n');
    return UNREF_X(x);
}