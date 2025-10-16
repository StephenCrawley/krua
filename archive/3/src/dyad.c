// functionality for dyad arithmetic and comparison ops

#include "dyad.h"
#include "object.h"

// dyad op macros
#define ADD(x,y) (x+y)
#define SUB(x,y) (x-y)
#define PRD(x,y) (x*y)
#define EQL(x,y) (x==y)
#define GRT(x,y) (x>y)
#define LST(x,y) (x<y)

#define DYAD_ITER_TYPE(op,rt,xt,yt) if    (xn==yn) for (i64 i=0; i<xn; i++) rt(r)[i] = op(xt(x)[i], yt(y)[i]); \
                                    else /*xn>yn*/ for (i64 i=0; i<xn; i++) rt(r)[i] = op(xt(x)[i], yt(y)[0]);  

#define DYAD_ITER(op) if      (axt==KI && ayt==KI)  { DYAD_ITER_TYPE(op,INT,INT,INT) } \
                      else if (axt==KI && ayt==KC)  { DYAD_ITER_TYPE(op,INT,INT,CHR) } \
                      else if (axt==KC && ayt==KI)  { DYAD_ITER_TYPE(op,INT,CHR,INT) } \
                      else  /*(axt==KC && ayt==KC)*/{ DYAD_ITER_TYPE(op,INT,CHR,CHR) } 

#define SWAP_XY() {K t=y; y=x,x=t; i8 tt=yt; yt=xt,xt=tt; i64 tn=yn; yn=xn,xn=tn; op=(op=='<')?'>':(op=='>')?'<':op;}

K execDyad(char op, K x, K y){
    // metadata
    i8 xt=TYP(x), yt=TYP(y);
    i64 xn=KCOUNT(x), yn=KCOUNT(y);

    // bail if vectors don't conform
    if (!IS_ATOM(x) && !IS_ATOM(y) && (xn != yn)){
        return UNREF_XY(kerr("'length! dyad operands must conform"));
    }

    // ensure x is generic if one of the args is generic
    if (xt && !yt) SWAP_XY();

    // if args are generic type, recurse
    if (!xt){
        K r=tn(KK,0),t;
        for (i64 i=0; i<xn; i++){
            t=execDyad(op, ref(OBJ(x)[i]), item(i,y));
            if (IS_ERROR(t)) return UNREF_XYR(t);
            r=jk(r,t);
        }
        return UNREF_XY(r);
    }

    // ensure x is a vector if one of the args is a vector
    if (IS_ATOM(x) || !IS_ATOM(y)) SWAP_XY();
    
    i8 axt=ABS(xt);
    i8 ayt=ABS(yt);

    // create return vector
    i8 rt=(axt == KC && ayt == KC && strchr("|&",op)) ? KC : KI;
    if (IS_ATOM(x)) rt = -rt;
    K r=tn(rt,xn);

    switch(op){
    case '+': DYAD_ITER(ADD); break;
    case '*': DYAD_ITER(PRD); break;
    case '=': DYAD_ITER(EQL); break;
    case '<': DYAD_ITER(LST); break;
    case '>': DYAD_ITER(GRT); break;
    case '&': if (ABS(rt)==KC){DYAD_ITER_TYPE(MIN,CHR,CHR,CHR)} else {DYAD_ITER(MIN)} break;
    case '|': if (ABS(rt)==KC){DYAD_ITER_TYPE(MAX,CHR,CHR,CHR)} else {DYAD_ITER(MAX)} break;
    default: r=UNREF_R(kerr("'nyi! dyad op"));
    }

    return UNREF_XY(r);
}

