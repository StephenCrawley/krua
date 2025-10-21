// krua tokenize -> parse -> compile -> vm

#include "eval.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

const K_char OPS[] = ":+-*%@.!,<>?#_~&|=$^";

// K bytecode:
// class  index
// 0      0  - 31: apply monadic operator
// 1      32 - 63: apply dyadic operator
// 2      64 - 95: apply n-adic operator
// 3      96 -127: push constant
// 4      128-159: get variable(local/arg/global)
// 5      160-191: set variable
// 6      192-223: ?
// 7      224-255: ?

// append a variable name to a vars list and return the bytecode+index
static K_char addVar(K *vars, K_sym x){
    return OP_GET_VAR + addSym(vars, x);
}

// append a K value to consts lists and return the bytecode+index
static K_char addConst(K *consts, K x){
    *consts = (*consts == 0) ? k1(x) : joinObj(*consts, x);
    return OP_CONST + HDR_COUNT(*consts)-1;
}

static K numbers(char *src, K_int count){
    K r;
    if (count == 1){
        K_int j = 0;
        do j = j*10 + (*src++ - '0'); while (ISDIGIT(*src));
        r = kint(j);
    } else {
        r = knew(KIntType, count);
        K_int *ints = INT_PTR(r);
        FOR_EACH(r){
            ints[i] = 0;
            do ints[i] = 10*ints[i] + (*src++ - '0'); while (ISDIGIT(*src));
            while (*src == ' ') ++src;
        }
    }
    return r;
}

// return index of next non-whitespace character
static int next(char *src, int i){
    while (src[i] == ' ') ++i;
    return i;
}

// return a token stream from source code
// potentially modifies the K objects in 'consts' and 'vars'
// a source constant/literal is appended to 'consts'
// variable names are appended to 'vars'
// NB: does not consume (unref) arg 'x' 
// NB: constant/variable tokens are the same value as the bytecode that's eventually generated for them
// TODO: write array of source code offsets which corresponds to each token for error reporting
K token(K x, K *vars, K *consts){
    K_int n = HDR_COUNT(x);
    // token stream to return
    K r = knew(KChrType, n);
    
    // loop over the source and generate tokens
    K_int i = 0;
    K_char *src = CHR_PTR(x);
    K_char *tok = CHR_PTR(r);
    while ((i = next(src, i)) < n){ // while next() non-whitespace position is valid/in source
        // token start index
        int t0 = i;

        if (ISALPHA(src[i])){
            // variables
            do ++i; while (ISALNUM(src[i]));    
            *tok++ = addVar(vars, encodeSym(src+t0, i-t0));
        } else if (ISDIGIT(src[i])){
            // numbers
            K_int count = 1;
            do {
                count += (src[i++] == ' ' && ISDIGIT(src[i]));
            } while (ISDIGIT(src[i]) || src[i] == ' ');
            *tok++ = addConst(consts, numbers(src+t0, count));
        } else if (src[i] == '"'){
            // string
            ++t0;
            do ++i; while (i < n && src[i] != '"');
            PARSE_ERROR(i == n, "unclosed string", unref(r));
            *tok++ = addConst(consts, (i-t0) == 1 ? kchr(src[t0]) : knewcopy(KChrType, i-t0, (K)(src+t0)));
            ++i;
        } else {
            // operators, punctutation
            K_char *op = strchr(OPS, src[i]);
            // TODO: allow punctuation
            //if (src[i] - 32 > 127u || !op){
            //    printf("'parse! invalid token:\n    %.*s\n%*s^\n", n, src, i+4, "");
            //    return UNREF_R(0);
            //}
            PARSE_ERROR(src[i] - 32 > 127u || !op, "invalid token", unref(r));
            *tok++ = op ? op - OPS : src[i];
            ++i;
        }
    }
    HDR_COUNT(r) = tok - CHR_PTR(r);
    return r;
}

// parse + compile token stream to bytecode
// NB: consumes (unrefs) arg 'x'
K compileExpr(K x){
    K_char *tok = (K_char*)x;
    K_int i = 0, j = 0, n = HDR_COUNT(x);
    K r = knew(KObjType, n);
    // -1+-2+3
    // -:1+-:2+3
    // -:+1-:+23
    // 32+-:1+-:
    while (i < n){
        if (tok[i] < 20){ // +x
            OBJ_PTR(r)[j] = kc1(OP_UNARY + tok[i]);
            i += 1;
        } else if (i+1 == n){
            OBJ_PTR(r)[j] = kc1(tok[i]);
            i += 1;
        } else if (i+1 < n && tok[i+1] < 20){ // x+
            OBJ_PTR(r)[j] = (!tok[i+1] && ISCLASS(OP_GET_VAR, tok[i])) ? kc1(OP_SET_VAR + tok[i]%32) : kc2(OP_BINARY + tok[i+1], tok[i]);
            i += 2;
        } else { // x y -> x@y
            OBJ_PTR(r)[j] = kc2(OP_BINARY + 5, tok[i]);
            i += 1;
        }
        j++;
    }
    HDR_COUNT(r) = j;
    //alternative. flatter version. write directly to bytecode array. untested
    //K_int m = n, i = 0, j = 0; for (int k=0, _n=n-1; k<_n; k++) m += (b[k]>=20 & b[k+1]>=20);
    //K ret = knew(KChrType, m); K_char *r = CHR_PTR(ret), *b = CHR_PTR(x), t;
    //while(i<n-1) { if(b[i]<20){r[j++]=b[i++]+OP_UNARY;} else if(b[i+1]<20){t=b[i++];r[j++]=OP_BINARY+b[i++];r[j++]=t;}else{r[j++]=0x45;r[j++]=b[i++]} }
    //if(i<n)r[j++]=b[i];
    //can we do it in reverse?

    // raze + reverse
    r = razeStr(r);
    K_char t, *s=CHR_PTR(r), *e=CHR_PTR(r)+HDR_COUNT(r)-1; while (s<e){t=*s;*s++=*e;*e--=t;}

    return UNREF_X(r);
}

// compile source code to bytecode
// returns (bytecode; variables; constants; sourcecode)
K compile(K src, K vars){
    K r, consts = 0;
    if (!(r = token(src, &vars, &consts))) goto cleanup;
    if (!(r = compileExpr(r))) goto cleanup;
    return k4(r, vars, consts, src);
cleanup:
    unref(vars), unref(consts), unref(src);
    return 0;
}

K getGlobal(K GLOBALS, K_sym var){
    K keys = KEYS(GLOBALS);
    K_int i = findSym(keys, var);
    VALUE_ERROR(i==HDR_COUNT(keys), "undefined variable: ", var, )
    return ref(OBJ_PTR(VALS(GLOBALS))[i]);
}

// interpret bytecode
// NB: does not consume (unref) any args
// limits:
// - 32 constants per expression
// - 32 variables per expression (incl. args/locals/gobals)
// TODO: multi-byte encoding
K vm(K x, K vars, K consts, K GLOBALS){
    K_sym *v = SYM_PTR(vars);
    K_char *ip = CHR_PTR(x), *e = ip + HDR_COUNT(x);
    K stack[STACK_SIZE], *top = stack+STACK_SIZE, *base = top, a; // stack grows down
    while (ip < e){
        K_char i = *ip & 31; // index: lower 5 bits
        switch(*ip++ >> 5){  // class: upper 3 bits
        case 0: NYI_ERROR(1, "vm: unary operation", goto bail) break;
        case 1: a=*top++; *top=dyad_table[i](a,*top); if (!*top) goto bail; break;
        case 2: NYI_ERROR(1, "vm: n-adic operation", goto bail) break;
        case 3: *--top=ref(OBJ_PTR(consts)[i]); break;
        case 4: *--top=getGlobal(GLOBALS,v[i]); if (!*top) goto bail; break;
        case 5: K*slot=getSlot(GLOBALS,v[i]); unref(*slot); *slot=ref(*top); break;
        }
    }
    return *top;
bail: while(top < base) unref(*top++); return 0;
}

void strip(K x){
    K_char *src = CHR_PTR(x);
    // line is comment
    if (*src == '/'){
        HDR_COUNT(x)=0;
        return;
    }
    // drop trailing comment
    // TODO: should be quote aware
    K_int n=HDR_COUNT(x), i=1;
    while (i < n && !(src[i] == '/' && src[i-1] == ' ')) ++i;
    // drop trailing whitespace
    while (i > 0 && src[i-1] == ' ') --i;
    HDR_COUNT(x) = i;
}

// evaluate a K-string
K eval(K x, K GLOBALS){
    // early exits
    if (HDR_COUNT(x) == 0) return UNREF_X(knull());
    if (CHR_PTR(x)[0] == '\\') exit(0);

    // strip comments
    strip(x);
    if (HDR_COUNT(x) == 0) return UNREF_X(knull());

    // compile to bytecode
    K r = compile(x, 0);
    if (!r) return 0;

    // call VM
    K bytecode = OBJ_PTR(r)[0];
    bool assignment = ISCLASS(OP_SET_VAR, CHR_PTR(bytecode)[HDR_COUNT(bytecode)-1]); // is last op assignment?
    r = UNREF_R(vm(bytecode, OBJ_PTR(r)[1], OBJ_PTR(r)[2], GLOBALS));
    return assignment ? UNREF_R(knull()) : r; // don't print if last op is assignment
}