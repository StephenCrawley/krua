#include "sym.h"
#include "krua.h"
#include "object.h"

// sym interning via separate-chain hash table
// linear hash. aggressive growth policy: rebalance if appending a new entry to non-0-count chain
// table: generic K list of chains
// chain: list of 64-bit ints (entries)
// entry: contains {hash, id}: bits 0-31=hash, 32-63=id (index in SYMS table)
// SYMS is a list of K strings (KChrType). this is our sym pool

K HTAB = 0, SYMS = 0;
static K_int split = 0; // the split index

static uint32_t djb2(K_int n, K_char *s){
    uint32_t h = 5381;
    FOR(n) h = ((h<<5) + h) + s[i];
    return h;
}

void initSymTab(){
    split = 0;
    SYMS = knew(KObjType, 0);
    HTAB = knew(KObjType, 4);
    FOR_EACH(HTAB) OBJ_PTR(HTAB)[i] = knew(KLngType, 0);
}

void freeSymTab(){
    unref(SYMS), unref(HTAB);
}

// split the chain at 'split' index when an insert lengthens a non-empty chain
static void splitChain(){
    K_int lvl = HDR_COUNT(HTAB) - split;
    K chain = OBJ_PTR(HTAB)[split], lo = knew(KLngType, 0), hi = knew(KLngType, 0);
    FOR_EACH(chain){
        K entry = LNG_PTR(chain)[i];
        if (((K_sym)entry & ((lvl<<1) - 1)) == (K_sym)(split + lvl))
            hi = joinTag(hi, entry);
        else
            lo = joinTag(lo, entry);
    }
    unref(chain);
    OBJ_PTR(HTAB)[split] = lo, HTAB = joinObj(HTAB, hi);
    if (2 * ++split == HDR_COUNT(HTAB)) split = 0;
}

K_sym internSym(K_int n, K_char *s){
    K_sym h = djb2(n, s);
    K_int lvl = HDR_COUNT(HTAB) - split, i = h & (lvl-1);
    if (i < split) i = h & ((lvl<<1) - 1);
    K chain = OBJ_PTR(HTAB)[i], *syms = OBJ_PTR(SYMS);
    FOR_EACH(chain){
        K entry = LNG_PTR(chain)[i];
        if (h == (K_sym)entry && n == HDR_COUNT(syms[entry >> 32]) && !memcmp(s, CHR_PTR(syms[entry >> 32]), n)){
            return entry >> 32; // return id (SYMS index)
        }
    }
    K_sym id = HDR_COUNT(SYMS); // new id
    SYMS = joinObj(SYMS, kstr(n, s));
    chain = joinTag(chain, ((K)id << 32) | h);
    OBJ_PTR(HTAB)[i] = chain;
    if (HDR_COUNT(chain) > 1) splitChain();
    return id;
}
