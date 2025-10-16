#include "t_parse.h"

#define T2(a,b)         (t[0]=a,t[1]=b)
#define T3(a,b,c)       (T2(a,b),t[2]=c)
#define T6(a,b,c,d,e,f) (T3(a,b,c),t[3]=d,t[4]=e,t[5]=f)

bool test_err(const char *s){
    K r=parse(s);
    return unref(r), KE==TYP(r);
}

// test type and count
// @s - source code to parse
// @r - parsed tree
// @t - expected type
// @n - expected count
// returns true if pass, false if fail
bool test_tn(const char *s, K r, i8 t, i64 n){
    // check type and count
    bool ft = (TYP(r)!=t);
    bool fn = (CNT(r)!=n);
    unref(r);
    if (ft || fn){
        printf("FAIL: %s -> ",s);
        if (ft) printf("typ ");
        if (fn) printf("cnt ");
        return false;
    }
    return true;
}

// test type and count, and type of child objects
// @s - source code to parse
// @t - expected type
// @n - expcted count
// @ct- child types; array of length n with expected types of child objects
// returns true if pass, false if fail
bool test(const char *s, i8 t, i64 n, char *ct){
    // parse the input
    K r = parse(s);
    // check type and count
    bool passed=test_tn(s,ref(r),t,n);
    // if no child types given, return result
    if (!ct) return unref(r), passed;
    // check child types. break on 1st fail
    for (i64 i=0; i<n; i++) {
        if( TYP(OBJ(r)[i]) != ct[i] ) { 
            if(passed){ printf("FAIL: %s -> ",s) ; passed=false; }
            printf ("child [%ld] typ. act:%d exp:%d ",i,TYP(OBJ(r)[i]),ct[i]);  
            break; 
        }
    }
    unref(r);
    if (WS){ if(passed)printf("FAIL: %s -> ",s); printf("ws %ld\n",WS); return false; }
    if (!passed) printf("\n");
    return passed;
}

int main(){
    bool r = true;  //results passed?
    char *s, t[10]; //string to parse, type array
    printf("Running parse tests...\n");

    // TEST 1
    s="1";
    r &= test(s, -KI, 1, 0);

    // TEST 2
    s="1 2";
    r &= test(s, KI, 2, 0);

    // TEST 3
    s="-1";
    r &= test(s, -KI, 1, 0);

    // TEST 4
    s="1 -2";
    r &= test(s, KI, 2, 0);

    // TEST 5
    s="1 - 2";
    T3(KV,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 6
    s="1- 2";
    T3(KV,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 7
    s="1-2";
    T3(KV,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 8
    s="-1 2";
    r &= test(s, KI, 2, 0);

    // TEST 9
    s="- 1 2";
    T2(KU,KI);
    r &= test(s, KK, 2, t);

    // TEST 10
    s="1--2";
    T3(KV,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 11
    s="1-:2";
    T3(KU,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 12
    s="1.2";
    r &= test(s, -KF, 1, 0);

    // TEST 13
    s="1.";
    r &= test(s, -KF, 1, 0);

    // TEST 14
    s=".2";
    r &= test(s, -KF, 1, 0);

    // TEST 15
    s="-.2";
    r &= test(s, -KF, 1, 0);
    
    // TEST 16
    s="-.2 3";
    r &= test(s, KF, 2, 0);
        
    // TEST 17
    s="2 -.3";
    r &= test(s, KF, 2, 0);

    // TEST 18
    s="1-.2";
    T3(KV,-KI,-KF);
    r &= test(s, KK, 3, t);

    // TEST 19
    s="1- .2";
    T3(KV,-KI,-KF);
    r &= test(s, KK, 3, t);

    // TEST 20
    s="1 - .2";
    T3(KV,-KI,-KF);
    r &= test(s, KK, 3, t);

    // TEST 21
    s="1 .";
    T2(KV,-KI);
    r &= test(s, KK, 2, t);

    // TEST 22
    s="(1;2)";
    T3(KU,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 23
    s="(1;2.)";
    T3(KU,-KI,-KF);
    r &= test(s, KK, 3, t);

    // TEST 24
    s="\"\"";
    r &= test(s, KC, 0, 0);

    // TEST 25
    s="\" \"";
    r &= test(s, -KC, 1, 0);

    // TEST 26
    s="\"  \"";
    r &= test(s, KC, 2, 0);

    // TEST 27
    s="`";
    r &= test(s, KS, 1, 0);

    // TEST 28
    s="``sa";
    t[0]=KS;
    r &= test(s, KK, 1, t);

    // TEST 29
    s="` `sa";
    t[0]=KS;
    r &= test(s, KK, 1, t);

    // TEST 30
    s="+[]";
    T2(KU,KU);
    r &= test(s, KK, 2, t);

    // TEST 31
    s="+[1]";
    T2(KU,-KI);
    r &= test(s, KK, 2, t);

    // TEST 32
    s="+[1;2]";
    T3(KV,-KI,-KI);
    r &= test(s, KK, 3, t);

    // TEST 33
    s="+";
    r &= test(s, KV, 1, 0);

    // TEST 34
    s="+:";
    r &= test(s, KU, 1, 0);

    // TEST 35
    s="+'";
    T2(KW,KU);
    r &= test(s, KK, 2, t);

    // TEST 36
    s="+/";
    T2(KW,KV);
    r &= test(s, KK, 2, t);

    // TEST 37
    s="{x}";
    //T3(KC,KS,-KS);
    T6(KX,KK,KC,KS,KK,KK);
    //r &= test(s, KL, 3, t);
    r &= test(s, KL, 6, t);

    // TEST 38
    s="{[]x}";
    //T3(KC,KS,-KS);
    T6(KX,KK,KC,KS,KK,KK);
    //r &= test(s, KL, 3, t);
    r &= test(s, KL, 6, t);

    // TEST 39
    s="{[x]x}";
    //T3(KC,KS,-KS);
    T6(KX,KK,KC,KS,KK,KK);
    //r &= test(s, KL, 3, t);
    r &= test(s, KL, 6, t);

    // TEST 40
    s="{[x;y]x+y}";
    //T3(KC,KS,KK);
    T6(KX,KK,KC,KS,KK,KK);
    //r &= test(s, KL, 3, t);
    r &= test(s, KL, 6, t);

    // TEST 41
    s="1(+)2";
    T2(-KI,KK);
    r &= test(s, KK, 2, t);

    // TEST 42
    s="x+y(-)";
    T3(KV,-KS,KK);
    r &= test(s, KK, 3, t);

    // TEST 43
    s="++1";
    T2(KU,KK);
    r &= test(s, KK, 2, t);

    // TEST 44
    s="(+)+1";
    T3(KV,KV,-KI);
    r &= test(s, KK, 3, t);

    // TEST 45
    s="*+`a`b!(1 2;\"ab\")";
    T2(KU,KK);
    r &= test(s, KK, 2, t);

    // TEST 46
    s="(!) . (`a`b;1 2)";
    T3(KV,KV,KK);
    r &= test(s, KK, 3, t);

    // ERRORS
    s="(";
    r &= test_err(s);

    s=")";
    r &= test_err(s);

    s="(1";
    r &= test_err(s);

    s="(1;";
    r &= test_err(s);

    s="(}";
    r &= test_err(s);

    s="\"foo";
    r &= test_err(s);

    s="1+2)";
    r &= test_err(s);

    s="{[x+1]x}";
    r &= test_err(s);

    s="1+1.2.3";
    r &= test_err(s);

    return r ? printf("ALL PARSE PASSED.\n"),0 : (printf("PARSE FAILED!!\n"),1);
}
