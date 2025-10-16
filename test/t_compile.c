#include <inttypes.h>
#include "../src/skrawl.h"
#include "../src/compile.h"

K parse(const char*);
K ref(K);
K printK(K);
void unref(K);
u64 WS;

#define NL() printf("\n")

// these are replicated (bad!) from compile.c 
#define BYTES(x)  (*OBJ(x))
#define CONSTS(x) (OBJ(x)[1])

bool test(const char *s, i8 t, i64 n, u8 *b){
    K x=compile(parse(s)), y;
    //printK(ref(x)); //debug
    bool bf=(KX!=TYP(BYTES(x)));
    bool nf=( n!=CNT(BYTES(x)));
    bool kf=(KK!=TYP(CONSTS(x)));
    if (bf||kf||nf){
        printf("COMPILE FAIL: %s -> ",s);
        if (bf) printf("BYTES typ ");
        if (nf) printf("BYTES cnt ");
        if (kf) printf("CONSTS typ ");
        printf("\n");
        return false;
    }

    y=BYTES(x);
    bool pass=true;
    for (u64 i=0; i<n; i++){
        if (b[i]!=CHR(y)[i]){
            printf("COMPILE FAIL: %s -> ",s);
            printf("INCORRECT BYTECODE...\n");
            printf("actual:   0x");for (u64 i=0; i<n; i++)printf("%02x",CHR(y)[i]);NL();
            printf("expected: 0x");for (u64 i=0; i<n; i++)printf("%02x",b[i]);NL();
            pass=false;
        }
    }
    unref(x);
    return pass;
}

int main(){
    bool pass=true;
    
    u8 e[]={2,OP_CONSTANT,0,OP_CONSTANT,1,OP_DYAD+TOK_PLUS,OP_RETURN};
    pass &= test("3+4", KK, sizeof(e), e);

    u8 e1[]={2,OP_CONSTANT,0,OP_CONSTANT,1,OP_DYAD+TOK_MINUS,OP_CONSTANT,2,OP_DYAD+TOK_PLUS,OP_RETURN};
    pass &= test("3+4-1", KK, sizeof(e1), e1);

    u8 e2[]={3,OP_CONSTANT,0,OP_CONSTANT,1,OP_CONSTANT,2,OP_APPLY_N,2,OP_RETURN};
    pass &= test("+:[1;2]", KK, sizeof(e2), e2);

    // WHO CARES? also less compiler code to handle this
    // f x, f@x, f[x] should generate same bytecode
    //u8 e3[]={OP_CONSTANT,0,OP_CONSTANT,1,OP_DYAD+TOK_AT,OP_RETURN};
    //pass &= test("f x", KK, sizeof(e3), e3);
    //pass &= test("f@x", KK, sizeof(e3), e3);
    //pass &= test("f[x]",KK, sizeof(e3), e3);

    // enlist (,1 and (2;3) forms) should generate OP_APPLY_N with ,: on top of stack
    //u8 e4[]={OP_CONSTANT,0,OP_CONSTANT,1,OP_APPLY_N,1,OP_RETURN};
    //pass &= test(",1", KK, sizeof(e4), e4);
    u8 e5[]={3,OP_CONSTANT,0,OP_CONSTANT,1,OP_CONSTANT,2,OP_APPLY_N,2,OP_RETURN};
    pass &= test("(2;3)", KK, sizeof(e5), e5);

    // elided args should create OP_APPLY_N instrad of eg OP_DYAD
    u8 e6[]={3,OP_CONSTANT,0,OP_CONSTANT,1,OP_CONSTANT,2,OP_APPLY_N,2,OP_RETURN};
    pass &= test("+[;1]", KK, sizeof(e6), e6);

    return pass ? printf("ALL COMPILE PASSED.\n"),0 : (printf("COMPILE FAILED!!\n"),1);
}
