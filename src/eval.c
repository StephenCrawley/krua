// krua tokenize -> compile -> vm

#include "eval.h"

#define ISALPHA(c) isalpha((int)(c))
#define ISDIGIT(c) isdigit((int)(c))
#define ISALNUM(c) isalnum((int)(c))

const K_char OPS[] = ":+-*%@.!,<>?#_~&|=$^";

// Global interpreter state (for lambda application in apply.c)
K GLOBALS = 0;

// forwards declarations
K load(K, K);

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

K params(K x){
    if (HDR_COUNT(x) == 0) return UNREF_X(knew(KSymType, 0)); // []
    x = cutStr(x, ';'); // cutStr consumes x
    K r = knew(KSymType, HDR_COUNT(x));
    // simply encode whatever is there. don't check if param names are valid
    K *p = OBJ_PTR(x);
    FOR_EACH(x){SYM_PTR(r)[i] = encodeSym(CHR_PTR(p[i]), HDR_COUNT(p[i]));}
    return UNREF_X(r);
}

// put local vars into `vars`. find locals by identifying assignment
// NB: does not consume (unref) arg 'src' 
void locals(K src, K *vars){
    K_char *s = CHR_PTR(src);
    for (K_int i = 1, n = HDR_COUNT(src); i < n; i++){
        if (s[i] == ':' && ISALPHA(s[i-1])){
            K_char *v = &s[i-1];
            while (v > s && ISALPHA(v[-1])) --v;
            addVar(vars, encodeSym(v, (s+i) - v));
        }
    }
}

// find matching close bracket, returns n if unmatched
static K_int findClose(K_char *s, K_int i, K_int n, K_char open, K_char close) {
    for (int d = 1; d && ++i < n; )
        d += (s[i] == open) - (s[i] == close);
    return i;
}

static K lambda(K_char *src, K_int start, K_int end){
    // ensure params list exists
    PARSE_ERROR(src[start+1] != '[', start+1, "lambda must have params {[a;b]a+b}", );
    K_char *pstart = src + start + 2; // skip {[
    K_char *pend = memchr(pstart, ']', end - start - 2);
    PARSE_ERROR(!pend, end, "unclosed param list", );
    
    // extract params and body
    K vars = params(knewcopy(KChrType, pend - pstart, (K)pstart));
    K_char argc = HDR_COUNT(vars);
    K body = knewcopy(KChrType, (src + end) - pend - 1, (K)(pend + 1));
    locals(body, &vars);
    K_char varc = HDR_COUNT(vars);
    
    K f = load(body, vars);
    if (!f) return 0;
    
    HDR_ARGC(f) = argc;
    HDR_VARC(f) = varc;
    HDR_TYPE(f) = KLambdaType;
    unref(OBJ_PTR(f)[3]);
    OBJ_PTR(f)[3] = knewcopy(KChrType, end - start + 1, (K)(src + start));
    return f;
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
        K_int t0 = i;

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
            PARSE_ERROR(i == n, i, "unclosed string", unref(r));
            *tok++ = addConst(consts, (i-t0) == 1 ? kchr(src[t0]) : knewcopy(KChrType, i-t0, (K)(src+t0)));
            ++i;
        } else if (src[i] == '{'){
            K_int end = findClose(src, i, n, '{', '}');
            PARSE_ERROR(end == n, i, "unclosed lambda", unref(r));
            K res = lambda(src, i, end);
            if (!res) return UNREF_R(0);
            *tok++ = addConst(consts, res);
            i = end + 1;
        } else if (src[i] == '(' || src[i] == ')'){
            if (src[i] == '(' && src[i+1] == ')'){
                // add empty list literal const ()
                *tok++ = addConst(consts, knew(KObjType, 0));
                i += 2;
            } else {
                *tok++ = src[i++];
            }
        } else {
            // operators, punctuation
            K_char *op = strchr(OPS, src[i]);
            // TODO: allow punctuation
            PARSE_ERROR(src[i] - 32 > 127u || (!op && src[i] != ';'), i, "invalid token", unref(r));
            *tok++ = op ? op - OPS : src[i];
            ++i;
        }
    }
    HDR_COUNT(r) = tok - CHR_PTR(r);
    return r;
}

// in-place reverse
#define REVERSE(r) K o, *s=OBJ_PTR(r), *e=OBJ_PTR(r)+HDR_COUNT(r)-1; while (s<e){o=*s;*s++=*e;*e--=o;};

// emit bytecode given token stream x
// fenced is an array of subexpression token streams from ()-delimited source code
static K emit(K x, K fenced) {
    K_char *tok = CHR_PTR(x); // token pointer
    K_int n = HDR_COUNT(x);
    if (!n) return knew(KChrType, 0);
    
    #define BC(c) ({K_char _c=(c); ISCLASS(OP_FENCED,_c) ? compile(OBJ_PTR(fenced)[_c&31], 1) : kc1(_c);})
    
    K_int i = 0, j = 0;
    K r = knew(KObjType, n);
    
    while (i < n) {
        if (tok[i] < 20) {  // +x
            OBJ_PTR(r)[j++] = kc1(OP_UNARY + tok[i++]);
        } else if (i+1 == n) {  // last token
            OBJ_PTR(r)[j++] = BC(tok[i++]);
        } else if (tok[i+1] < 20) {  // x: or x+ 
            OBJ_PTR(r)[j++] = (!tok[i+1] && ISCLASS(OP_GET_VAR, tok[i])) ? 
                kc1(OP_SET_VAR + tok[i]%32) : razeStr(k2(BC(tok[i]), kc1(OP_BINARY + tok[i+1])));
            i += 2;
        } else {  // x y
            OBJ_PTR(r)[j++] = razeStr(k2(BC(tok[i++]), kc1(OP_BINARY + 5)));
        }
    }
    HDR_COUNT(r) = j;
    
    #undef BC
    
    // reverse
    REVERSE(r);
    
    return razeStr(r);
}

// extract fenced expressions (parens), then compile
K compile(K x, bool f) {
    K fenced = 0;
    K_char *t = CHR_PTR(x);
    K_int n = HDR_COUNT(x), j = 0;
    
    for (K_int i = 0; i < n; ) {
        if (t[i] == '(') {
            K_int end = findClose(t, i, n, '(', ')');
            PARSE_ERROR(end == n, -1, "unmatched (", unref(fenced));
            K body = knewcopy(KChrType, end - i - 1, (K)(t + i + 1));
            fenced = fenced ? joinObj(fenced, body) : k1(body);
            t[j++] = OP_FENCED + HDR_COUNT(fenced) - 1;
            i = end + 1;
        } else {
            PARSE_ERROR(t[i] == ')', -1, "unmatched )", unref(fenced));
            t[j++] = t[i++];
        }
    }
    HDR_COUNT(x) = j;

    // cut on ; and emit tokens for each subexpression
    x = cutStr(ref(x), ';');
    K r = knew(KObjType, HDR_COUNT(x));
    K *ret = OBJ_PTR(r), *expr = OBJ_PTR(x);
    FOR_EACH(x) ret[i] = emit(expr[i], fenced);
    if (f && HDR_COUNT(x) > 1){ // is a fenced expression?
        REVERSE(r);
        r = joinObj(r, kc2(OP_ENLIST, HDR_COUNT(r)));
        r = razeStr(r);
    } else if (f){
        r = razeStr(r);
    } else {
        r = joinStr(r, OP_POP);
    }

    unref(fenced);
    return UNREF_X(r);
}

#undef REVERSE

// compile source code to bytecode and vars/consts
// returns (bytecode; variables; constants; sourcecode)
K load(K src, K vars){
    K tokens, bytecode, consts = 0;
    tokens = token(src, &vars, &consts);
    if (!tokens) goto cleanup;
    bytecode = compile(tokens, 0);
    unref(tokens);
    if (!bytecode) goto cleanup;
    return k4(bytecode, vars, consts, src);
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
// - 32 variables per expression (incl. args/locals/globals)
// TODO: multi-byte encoding
K vm(K x, K vars, K consts, K GLOBALS, K_char varc, K*args){
    if (HDR_COUNT(x) == 0) return knull();  // Empty bytecode
    K_sym *v = SYM_PTR(vars);
    K_char *ip = CHR_PTR(x), *e = ip + HDR_COUNT(x);
    K stack[STACK_SIZE], *top = stack+STACK_SIZE, *base = top, a; // stack grows down
    while (ip < e){
        K_char i = *ip & 31; // index: lower 5 bits
        switch(*ip++ >> 5){  // class: upper 3 bits
        case 0: NYI_ERROR(1, "vm: unary operation", goto bail) break;
        case 1: a=*top++; *top=dyad_table[i](a,*top); if (!*top) goto bail; break;
        case 2: if(!i) unref(*top++); else NYI_ERROR(1, "vm: n-adic operation", goto bail) break;
        case 3: *--top=ref(OBJ_PTR(consts)[i]); break;
        case 4: *--top=i<varc?ref(args[i]):getGlobal(GLOBALS,v[i]); if (!*top) goto bail; break;
        case 5: K*slot=i<varc?args+i:getSlot(GLOBALS,v[i]); unref(*slot); *slot=ref(*top); break;
        case 7: if(!i){a=knew(KObjType,*ip++);FOR_EACH(a)OBJ_PTR(a)[i]=*top++;*--top=squeeze(a); break;} 
                NYI_ERROR(1, "vm: OP_SPECIAL", goto bail);
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
K eval(K x, K GLOBALS_param){
    GLOBALS = GLOBALS_param;  // Set global for access by apply()

    // early exits
    if (HDR_COUNT(x) == 0) return UNREF_X(knull());
    if (CHR_PTR(x)[0] == '\\') exit(0);

    // strip comments
    strip(x);
    if (HDR_COUNT(x) == 0) return UNREF_X(knull());

    // load source for execution on VM
    K r = load(x, 0);
    if (!r) return 0;

    // check bytecode for OP_SET_VAR or OP_POP as final byte. if yes, return null
    K bytecode = OBJ_PTR(r)[0];
    K_char lastOp = CHR_PTR(bytecode)[HDR_COUNT(bytecode)-1];
    bool returnNull = lastOp == OP_POP || ISCLASS(OP_SET_VAR, lastOp); // is last op assignment or OP_POP?
    
    // call VM
    r = UNREF_R(vm(bytecode, OBJ_PTR(r)[1], OBJ_PTR(r)[2], GLOBALS, 0, 0));
    return r && returnNull ? UNREF_R(knull()) : r; // don't print if last op is assignment
}