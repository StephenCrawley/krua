#include "object.h"
#include "verb.h"
#define __USE_MISC //for MAP_ANONYMOUS declaration
#include <sys/mman.h>
#undef __USE_MISC

// memory (buddy) allocation //

// the K type is an unsigned 64-bit int
// K objects are either a tagged int or a pointer to data

// tagged objects contain the type in the upper 8 bits, and the value in the lower 56 bits
// memory for tagged objects doesn't need to be managed manually 

// otherwise the K is a pointer to data 
// pointed-to K objects are allocated on the heap using a buddy allocation algorithm
// these objects occupy an x^2 bucket of memory
// the K object header is 16 bytes (see skrawl.h for header layout) so *theoretically* objects need to be assigned a minimum of 16 bytes
// however 16B would only accommodate a 0-count object, and in practice we usually have objects with at least 1 element
// also an object in the free list (see next paragraph) is a node in a linked list so needs space for a pointer to the next free object
// therefore the minimum size MIN_ALLOC is 32 bytes 
// the K pointer points to just after the header, to the start of the data. see skrawl.h for header and data accessors
// example: if we wanted to allocate an array of 22 chars, we would need 16(header) + 22(data) = 38 bytes
// 38 bytes fits into a 64-byte bucket. this leaves 26 bytes of space in the tail
// this may seems wasteful but allows for in-place appends if there are no references to the object

// free K buckets are available in the global M array
// K objects are ref counted with ref(increment) and unref(decrement)
// when an object is "freed" (unref called when object ref is already 0) it is placed into M
// each element of M contains a linked list of free objects
// M[n] contains a linked list of buckets of size ( MIN_ALLOC<<n )
// so M[0] has 32B buckets, M[1] 64B buckets, M[2] 128B, ...
// NB: mmap'd memory is never returned to the OS, and free buckets are never coalesced with their buddies into larger buckets
// TODO: implement coalesce&free functionality

#define HEADER_SIZE    16
#define MIN_ALLOC      32L 
#define BUCKET_SIZE(x) (u64)( MIN_ALLOC << MEM(x) ) //size of bucket containing object x
#define SIZEOF(x)      ( TYPE_SIZE[ABS(TYP(x))] )   //elemental size of x

// forward declarations
static void _printK(K x);

// global vars
static const char   *VERB_STR=":+-*%,?.@!$#_^&=<>~|0;";
static const char *ADVERB_STR="'/\\'/\\";

// mmap wrapper 
static void *alloc_n(u64 n){
    void *x = mmap(0,n,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    if (x != MAP_FAILED)
        return x;
    else 
        printf("'mmap failed! exiting...\n"), exit(1);
}

#ifdef DBG_WS
u64 WS=0;  // number of bytes allocated to objects (can be less than bytes requested from the OS)
u64 WT=0;  // total number of bytes allocated on the heap
#endif

K M[31]; // array of free memory buckets

// return smallest bucket with at least n bytes
static K m1(u64 n){
    K x, r;
    i8 bucket, j;

    // find which bucket we need to assign from
    // the K header is 16 bytes
    // we add 15 so the arg to clzll is guarenteed to be at least 16 (n must be >0) 
    // we subtract the clz from 59 as we want bucket 0 to be a size of 2^5 bytes
    // (if 0 bytes was the smallest allocation we would subtract from 64)
    bucket = 59 - __builtin_clzll(n + 15);
    j = bucket;
    
    // if the bucket exists, return it
    x = M[bucket];
    if (x){
        M[bucket] = *OBJ(x); //set the next bucket in the linked list as the top of the free list in M
        return x;
    }

    // else we search for larger bucket in M we can split
    // if none exists we mmap a new block of memory with alloc_n
    // MAX(19, bucket) means 32L<<19 will always be the minimum allocation (16KiB)
    do {
        ++j;
        x = ( j < 31 ) ?
            M[j] :
            HEADER_SIZE + (K)alloc_n(MIN_ALLOC << (j=MAX(19, bucket)));
    } while (!x);

    MEM(x) = bucket, M[j] = *OBJ(x), r = x;

    // iteratively split the mmap'd block into smaller buckets and add the buckets to M
    while (bucket < j){
        // jump to next bucket
        x += BUCKET_SIZE(x),
        // assign bucket val in header
        MEM(x) = bucket,
        // add new bucket to the free bucket array
        M[bucket] = x,
        // 0 its 1st element (not a linked list)
        *OBJ(x) = 0,
        // next bucket
        ++bucket;
    }

    return r;
}

// size of each type     KK KX KC KI KF KS KD KT KL KP KQ  '  /  \ ': /: \: KU KV KW 
static i8 TYPE_SIZE[] = { 8, 1, 1, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};

// return a K object of type t and count n
K tn(i8 t, i64 n){
    K r = m1( MAX(1, n) * TYPE_SIZE[ABS(t)] );

#ifdef DBG_WS
    WS += BUCKET_SIZE(r);
    WT += BUCKET_SIZE(r);
#endif

    HDR_TYP(r) = t, REF(r) = 0, HDR_CNT(r) = n;
    return r;
}

// copy y into x starting at x[i]
static K xiy(K x, i64 i, K y){
    i8 s = SIZEOF(x);
    memcpy(CHR(x)+s*i, (void*)y, s*CNT(y));
    if (IS_GENERIC(y))
        for (i64 i=0,n=CNT(y); i<n; i++){ ref(OBJ(y)[i]); }
    return UNREF_Y(x);
}

// join 2 K objects
K j2(K x, K y){
    // if x is empty, return y
    if (!CNT(x)) return UNREF_X(y);

    i8  s=SIZEOF(x);
    i8  t=ABS(TYP(x));
    i64 m=CNT(x);
    i64 n=CNT(y)+m;
    u64 b=s*n;  //number of bytes used by elements in joined object

    // if x has refcount=0 and enough space to append y's items, we append in place
    // otherwise we create a new object and copy in x and y
    if (REF(x) || BUCKET_SIZE(x) < b+HEADER_SIZE){
        x=xiy(tn(t,n),0,x);
    }
    else {
        HDR_CNT(x)=n, HDR_TYP(x)=t;
    }

    return TAG_TYP(y) ? (K)memcpy(CHR(x)+s*m,&y,s) : xiy(x,m,y);
}

K jk(K x, K y){
    return j2(x,k1(y));
}

// join list of strings x with separator c
K js(K x, char c){
    // if c!=0 we need xn extra chars for the separators
    i64 xn=CNT(x), n=c?xn:0;
    // add the length of each string
    for (i64 i=0; i<xn; i++) n+=CNT(OBJ(x)[i]);
    // create return object
    K r=tn(KC,n), t;
    // ignore last separator
    if (c) --HDR_CNT(r);
    u8 *s=CHR(r);
    // append strings and separator
    for (i64 i=0; i<xn; i++){
        t=OBJ(x)[i];
        n=CNT(t);
        s=n+(u8*)memcpy(s,CHR(t),n);
        if (c) *s++ = c;
    }
    return UNREF_X(r);
}

i8 ksize(K x){
    return SIZEOF(x);
}

// box x
K k1(K x){
    K r=tn(KK,1);
    *OBJ(r)=x;
    return r;
}

// return (x;y)
K k2(K x, K y){
    K r=tn(KK,2);
    OBJ(r)[0]=x;
    OBJ(r)[1]=y;
    return r;
}

// return (x;y;z)
K k3(K x, K y, K z){
    K r=tn(KK,3);
    OBJ(r)[0]=x;
    OBJ(r)[1]=y;
    OBJ(r)[2]=z;
    return r;
}

K kn(K*x, i64 n){
    K r=tn(KK,n);
    while (n--) OBJ(r)[n]=x[n];
    return r;
}

// create byte atom
K kx(u8 c){
    K r=tn(-KX,1);
    *CHR(r)=c;
    return r;
}

// create 2-byte vector
K kx2(u8 x,u8 y){
    K r=tn(KX,2);
    CHR(r)[0]=x;
    CHR(r)[1]=y;
    return r;
}

// create char atom
K kc(u8 c){
    K r=tn(-KC,1);
    CHR(r)[0]=c;
    return r;
}

// create char vector with count n from string
K kCn(char *s, i64 n){
    return (K)memcpy((void*)tn(KC,n),s,n);
}
// create char vector with 0-terminated string
K kC0(char *s){
    return kCn(s,strlen(s));
}

// create int atom
K ki(i64 x){
    K r=tn(-KI,1);
    INT(r)[0]=x;
    return r;
}

// create float atom
K kf(double x){
    K r=tn(-KF,1);
    FLT(r)[0]=x;
    return r;
}

// create int atom
K ks(i64 x){
    K r=tn(-KS,1);
    INT(r)[0]=x;
    return r;
}

// make dict
K kD(K x, K y){
    return tx(KD,k2(x,y));
}

// make table from dict
K kT(K x){
    return tx(KT,k1(x));
}

// create monadic verb
K ku(u64 i){
    return SET_TAG(KU,i);
}

// create dyadic verb
K kv(u64 i){
    return SET_TAG(KV,i);
}

// create adverb
K kw(i8 t){
    return SET_TAG(KW,t);
}

// create derived verb 
K kwx(i8 t, K x){
    K r=tx(t,k1(x));
    i8 rank=rankOf(x);
    if (rank == 2 && (t == KOVER || t == KSCAN)) rank=-rank;
    HDR_RNK(r)=rank;
    return r;
}

// create composition
K kq(K x, K y){
    K r=tx(KQ,k2(x,y));
    HDR_RNK(r)=rankOf(y);
    return r;
}

// get verb char from verb enum i
u8 cverb(u8 i){
    return VERB_STR[i];
}

// get adverb char from adverb enum i
u8 cadverb(u8 i){
    return ADVERB_STR[i];
}

// get adverb enum from adverb char c
u64 iadverb(char c){
    return ic((char*)ADVERB_STR,c);
}

// create monad type from verb char
K kuc(char c){
    return ku(ic((char*)VERB_STR,c));
}

// create dyad type from verb char
K kvc(char c){
    return kv(ic((char*)VERB_STR,c));
}

// return magic value (elided list/lambda args)
K km(){
    return SET_TAG(KM,0);
} 

// create error object from x
// if TYP(x)==KK join the strings
K ke(K x){
    return tx(KE,TYP(x)?x:js(x,'\n'));
}

// create error object from 0-terminated string
K kerr(char *s){
    return ke(kC0(s));
}

// decrement K object refcount and place it back in M if no longer referenced
void unref(K x){
    // return tagged K
    if (TAG_TYP(x)) return; 

    // if refcount isn't 0, decrement and return
    if (REF(x)) { --REF(x); return; };
    
    // if generic K object, unref child objects
    if (IS_GENERIC(x))
        for (i64 i=0, n=CNT(x); i<n; i++) unref( OBJ(x)[i] );

#ifdef DBG_WS
    WS -= BUCKET_SIZE(x);
#endif

    // link top of free list, then set object as top of free list in M
    *OBJ(x) = M[MEM(x)];
    M[MEM(x)] = x;
}

// increment refcount
K ref(K x){
    return TAG_TYP(x) ? x : (REF(x)++, x);
}

// replace *x with y.
// used in amend4(apply.c) and assignment(vm.c)
void replace(K *x, K y){
    unref(*x);
    *x=y;
}

// squeeze conforming dicts into table
// (`a`b!1 2;`a`b!3 4) -> +`a`b!(1 3;2 4)
K squeezeDict(K x){
    // we will use the 1st dict to compare against other elements
    K key=KEY(*OBJ(x));
    if (TYP(key)!=KS)
        return x;

    // iterate x and check dicts conform
    i64 n=CNT(x);
    i64 m=CNT(key);
    for (i64 i=1; i<n; i++){
        K t=OBJ(x)[i];
        // check item is a dict
        if (TYP(t)!=KD)
            return x;
        // and the keys match
        K key2=KEY(t);
        if (TYP(key2)!=KS)
            return x;
        if (CNT(key2)!=m)
            return x;
        if (memcmp(CHR(key2),CHR(key),ksize(key)*CNT(key2)))
            return x;
    }

    // create columns
    K r=tn(KK,m);
    for (i64 i=0; i<m; i++)
        OBJ(r)[i]=tn(KK,n);
    
    // populate columns
    for (i64 i=0; i<n; i++)
        for (i64 j=0; j<m; j++)
            OBJ(OBJ(r)[j])[i]=item(j,VAL(OBJ(x)[i]));

    // squeeze columns
    for (i64 i=0; i<m; i++)
        OBJ(r)[i]=squeeze(OBJ(r)[i]);

    // return object
    r=kD(ref(key),r);
    return UNREF_X(kT(r));
}

// squeeze into more compact form if possible
// ("a";"b") -> "ab"
K squeeze(K x){
    K r;
    i8 t;
    i64 n=CNT(x);

    // if not a general list or count=0, return x
    if (TYP(x) || !n) return x;

    t=TYP(*OBJ(x));
    // if first element dict, call squeezeDict
    if (t==KD) return squeezeDict(x);

    // if 1st element not atom, return x
    if (t>=0) return x;

    // if not all same type, return x
    for (i64 i=1; i<n; i++)
        if (TYP(OBJ(x)[i]) != t) return x;

    r=tn(-t,n);
    for (i64 i=0, b=SIZEOF(r); i<n; i++)
        memcpy(CHR(r)+b*i, (void*)OBJ(x)[i], b);

    return unref(x), r;
}

// return x[i]
// ref if x[i] is an object
K item(i64 i, K x){
    if (IS_ATOM(x)) return ref(x);

    i8 xt=TYP(x);

    if (xt==KT){
        // extract underlying dict
        K dict=*OBJ(x);

        // return object, and values (columns)
        K r=tn(KK,0);
        K val=VAL(dict);

        // iterate and index each column 
        for (i64 j=0,n=CNT(val); j<n; j++)
            r=jk(r,item(i,OBJ(val)[j]));

        return kD(ref(KEY(dict)),squeeze(r));
    }

    return
        xt==KK ? ref(OBJ(x)[i]) :
        xt==KC ?  kc(CHR(x)[i]) :
        xt==KI ?  ki(INT(x)[i]) :
        xt==KF ?  kf(FLT(x)[i]) :
                  ks(INT(x)[i]) ;
}

// "ab" -> ("a";"b")
K expand(K x){
    i64 n=KCOUNT(x);
    K r=tn(KK,n);
    for (i64 i=0; i<n; i++)
        OBJ(r)[i]=item(i,x);
    return UNREF_X(r);
}


// ,1 -> ,1
K enlist(K x){
    return squeeze(k1(x));
}

// copy n items from index i
K sublist(K x, i64 i, i64 n){
    i8 s=SIZEOF(x);
    K r=tn(TYP(x),n);
    memcpy(CHR(r),CHR(x)+(s*i),s*n);
    if (IS_GENERIC(r))
        for (i64 i=0; i<n; i++) { ref(OBJ(r)[i]); }
    return UNREF_X(r);
}

// reuse x if no refs. else make a copy
K reuse(K x){
    return REF(x) ? xiy(tn(TYP(x),CNT(x)),0,x) : x;
}

// printer functions //

static void printInt(K x){
    for (i64 i=0, n=CNT(x), last=n-1; i<n; i++){
        i64 j=INT(x)[i];
        INULL==j ? printf("0N") : printf("%ld",j);
        if (i!=last) putchar(' ');
    }
}

static void printFlt(K x){
    for (i64 i=0, n=CNT(x), last=n-1; i<n; i++){
        double f=FLT(x)[i];
        f!=f ? printf("0n") : printf("%f",f);
        if (i!=last) putchar(' ');
    }
}

static void printSym(K x){
    // syms are encoded in i64, so max 8 chars
    char s[10] = {'`', 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (i64 i=0, n=CNT(x), b=sizeof(i64); i<n; i++){
        memcpy(&s[1], (void*)(INT(x)+i), b);
        printf("%s", s);
    }
}

static void _printK(K x){
    i8  xt=IS_DERIVED_VERB(x) ? K_ADVERB_START : TYP(x);
    if (xt==KL){ x=LAMBDA_STRING(x); }
    i64 n=CNT(x);
    
    if (n==1 && (!xt || IS_SIMPLE_LIST(x))) putchar(',');

    switch(ABS(xt)){
    case KK: if(1!=n)putchar('('); for (i64 i=0, last=n-1; i<n; i++){ _printK( OBJ(x)[i] ); if(i!=last)putchar(';'); } if(1!=n)putchar(')'); break;
    case KX: printf("0x"); for (i64 i=0; i<n; i++){ printf("%02x",CHR(x)[i]); } break;
    case KC: putchar('"'); for (i64 i=0; i<n; i++){ putchar(CHR(x)[i]); } putchar('"'); break;
    case KI: n?printInt(x):(void)printf("!0"); break;
    case KF: n?printFlt(x):(void)printf("0#0."); break;
    case KS: n?printSym(x):(void)printf("0#`"); break;
    case KD: _printK(KEY(x)), putchar('!'), _printK(VAL(x)); break;
    case KT: putchar('+'); _printK(*OBJ(x)); break;
    case KU: putchar(VERB_STR[TAG_VAL(x)]); putchar(':'); break;
    case KV: putchar(VERB_STR[TAG_VAL(x)]); break;
    case KW: putchar(ADVERB_STR[TAG_VAL(x)]); if (2<TAG_VAL(x)) putchar(':'); break;
    case K_ADVERB_START: _printK(*OBJ(x)); _printK(kw(TYP(x)-K_ADVERB_START)); break;
    case KE: // fall through
    case KL: fwrite(CHR(x), sizeof(char), n, stdout); break;
    case KP: _printK(*OBJ(x)); putchar('['); for (i64 i=1,last=n-1; i<n; i++){_printK(OBJ(x)[i]);if(i!=last)putchar(';');}putchar(']'); break;
    case KQ: _printK(OBJ(x)[0]),_printK(OBJ(x)[1]); break;
    case KM: break;
    default: printf("'nyi! print type %d\n", TYP(x));
    }
}

// print a K object. consumes the argument
K printK(K x){
    if (IS_NULL(x) || IS_MONAD(x,TOK_COLON))
        return x;

    _printK(x);
    putchar('\n');
    return unref(x), x;
}
