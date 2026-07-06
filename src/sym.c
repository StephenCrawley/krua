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

static uint32_t djb2(K_int n, K_char *s){
    uint32_t h = 5381;
    FOR(n) h = ((h<<5) + h) + s[i];
    return h;
}

void initSymTab(){
    SYMS = knew(KObjType, 0);
    HTAB = knew(KObjType, 4);
    FOR_EACH(HTAB) OBJ_PTR(HTAB)[i] = knew(KLngType, 0);
}

void freeSymTab(){
    unref(SYMS), unref(HTAB);
}

// split entries in chain at 'split' index between old and newly appended chain
static void splitChain(){
    K_int m = HDR_COUNT(HTAB), maxsplit = stdc_bit_floor((K_sym)m), split = m - maxsplit;
    K chain = OBJ_PTR(HTAB)[split], lo = knew(KLngType, 0), hi = knew(KLngType, 0);
    FOR_EACH(chain){
        K entry = LNG_PTR(chain)[i];
        if ((K_sym)entry & maxsplit) // maxsplit's single bit is the only bit of hash%(2*maxsplit) that varies within a chain
            hi = joinTag(hi, entry);
        else
            lo = joinTag(lo, entry);
    }
    unref(chain);
    OBJ_PTR(HTAB)[split] = lo, HTAB = joinObj(HTAB, hi); // hi lands at index m
}

// h = hashed sym. m = number of chains. maxsplit = split index bound: highest pow2 <= m, so split = m-maxsplit
K_sym internSym(K_int n, K_char *s){
    K_sym h = djb2(n, s);
    K_int m = HDR_COUNT(HTAB), maxsplit = stdc_bit_floor((K_sym)m), i = h & (2*maxsplit - 1);
    if (i >= m) i -= maxsplit;
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
