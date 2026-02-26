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
// 7      224-255: special operations (enlist, pop)

// append a variable name to a vars list and return the bytecode+index
static K_char addVar(K *vars, K_sym x){
    return OP_GET_VAR + addSym(vars, x);
}

// append a K value to consts lists and return the bytecode+index
static K_char addConst(K *consts, K x){
    *consts = (*consts == 0) ? k1(x) : joinObj(*consts, x);
    return OP_CONST + HDR_COUNT(*consts)-1;
}

static K numbers(char *src, K_int len, K_int count){
    K r;
    char *end = src + len;
    if (count == 1){
        K_int j = 0;
        do j = j*10 + (*src++ - '0'); while (src < end && ISDIGIT(*src));
        r = kint(j);
    } else {
        r = knew(KIntType, count);
        K_int *ints = INT_PTR(r);
        FOR_EACH(r){
            ints[i] = 0;
            do ints[i] = 10*ints[i] + (*src++ - '0'); while (src < end && ISDIGIT(*src));
            while (src < end && *src == ' ') ++src;
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

// put local vars into 'vars'. find locals by identifying assignment
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

// return index of close bracket (>0), returns 0 if unmatched
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
    // NB: 'vars' will contain params, local vars, and global vars referenced in the lambda
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
            do ++i; while (i < n && ISALNUM(src[i]));
            *tok++ = addVar(vars, encodeSym(src+t0, i-t0));
        } else if (ISDIGIT(src[i])){
            // numbers
            K_int count = 1;
            do {
                count += (src[i++] == ' ' && ISDIGIT(src[i]));
            } while (i < n && (ISDIGIT(src[i]) || src[i] == ' '));
            *tok++ = addConst(consts, numbers(src+t0, i-t0, count));
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
            PARSE_ERROR(src[i] - 32 > 94u, i, "invalid token", unref(r));
            K_char *op = strchr(OPS, src[i]);
            *tok++ = op ? op - OPS : src[i];
            ++i;
        }
    }
    HDR_COUNT(r) = tok - CHR_PTR(r);
    return r;
}

// placeholder token ranges for reduced token 
enum {
    TOK_PAREN   = 0x40,
    TOK_BRACKET = 0xc0,
    TOK_POSTFIX = 0xe0,
};

// in-place reverse of K list
static void reverse(K r){
    K o, *s=OBJ_PTR(r), *e=OBJ_PTR(r)+HDR_COUNT(r)-1; while (s<e){o=*s;*s++=*e;*e--=o;};
}
// in-place reverse of K_char list
static void reverseChr(K r){
    K_char o, *s=CHR_PTR(r), *e=CHR_PTR(r)+HDR_COUNT(r)-1; while (s<e){o=*s;*s++=*e;*e--=o;};
}

static K expandPostfix(K x, K fenced, K postfix);

static K expandSwitch(K_char c, K fenced, K postfix){
    if (ISCLASS(TOK_PAREN, c)) return compile(0, ref(OBJ_PTR(fenced)[c & 31]), 1);
    if (ISCLASS(TOK_POSTFIX, c)) return expandPostfix(OBJ_PTR(postfix)[c & 31], fenced, postfix);
    return kc1(c);
}

static K expandPostfix(K x, K fenced, K postfix){
    K_char *b = CHR_PTR(x);
    K r = expandSwitch(*b, fenced, postfix);
    if (!r) return 0;
    for (K_int i = 1, n = HDR_COUNT(x); i < n; i++) {
        r = compile(r, ref(OBJ_PTR(fenced)[b[i]&31]), 2);
        if (!r) return UNREF_R(0);
    }
    return r;
}

static K rpn(K x){
    K_int i = 0, j = 0, n = HDR_COUNT(x);
    K r = knew(KChrType, n * 2);
    K_char *xp = CHR_PTR(x), *rp = CHR_PTR(r);
    while (i < n){
        if (i+1 == n || xp[i] < 20){
            rp[j++] = xp[i++];
        } else if (xp[i+1] < 20){
            if (!xp[i+1] && ISCLASS(OP_GET_VAR, xp[i])){
                rp[j++] = OP_SET_VAR + xp[i]%32;
            } else {
                rp[j++] = OP_BINARY + xp[i+1];
                rp[j++] = xp[i]; 
            }
            i += 2;
        } else {
            rp[j++] = OP_BINARY + 5;
            rp[j++] = xp[i++];
        }
    }
    HDR_COUNT(r) = j;
    reverseChr(r);
    return r;
}

static K expandTokens(K x, K fenced, K postfix){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = expandSwitch(CHR_PTR(x)[i], fenced, postfix);
        if (!t) return UNREF_R(0); 
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X(razeStr(r));
}

static K reduceFenced(K x, K *fenced){
    K_int j = 0;
    K_char *tok = CHR_PTR(x);
    for (K_int i = 0, n = HDR_COUNT(x); i < n; ) {
        if (tok[i] == '(' || tok[i] == '[') {
            bool p = tok[i] == '(';
            K_int end = findClose(tok, i, n, tok[i], p?')':']');
            if (!end) return UNREF_X(0);
            K body = knewcopy(KChrType, end - i - 1, (K)(tok + i + 1));
            *fenced = *fenced ? joinObj(*fenced, body) : k1(body);
            tok[j++] = (p ? TOK_PAREN : TOK_BRACKET) + HDR_COUNT(*fenced) - 1;
            i = end + 1;
        } else {
            tok[j++] = tok[i++];
        }
    }
    HDR_COUNT(x) = j;
    return x;
}

static K reducePostfix(K x, K *postfix){
    K_int j = 0;
    K_char *tok = CHR_PTR(x);
    for (K_int i = 0, n = HDR_COUNT(x); i < n; ){
        if (i < n-1 && ISCLASS(TOK_BRACKET, tok[i+1])){
            K_int start = i++;
            do ++i; while (i<n && ISCLASS(TOK_BRACKET, tok[i]));
            K body = knewcopy(KChrType, i - start, (K)(tok + start));
            *postfix = *postfix ? joinObj(*postfix, body) : k1(body);
            tok[j++] = TOK_POSTFIX + HDR_COUNT(*postfix) - 1;
        } else {
            tok[j++] = tok[i++];
        }
    }
    HDR_COUNT(x) = j;
    return x;
}

// extract fenced expressions (parens), then compile
// decomposed into several passes:
// 1. reduce () and [] to single tokens
// 2. reduce postfix f[..] and f/ (nyi) to single tokens
// 3. cut bytecode into slices on ';'
// 4. emit:
//     restructure as reverse polish notation
//     expand (recursively) the reduced structures
// 5. stitch back the slices of bytecode
K compile(K f, K x, int mode) {
    K fenced = 0, postfix = 0, r = 0;
    if (!(x = reduceFenced(x, &fenced))) goto cleanup;
    x = reducePostfix(x, &postfix);

    // cut on ; and emit tokens for each subexpression
    x = cutStr(x, ';');
    K_int n = HDR_COUNT(x);
    r = knew(KObjType, n);
    K *ret = OBJ_PTR(r), *expr = OBJ_PTR(x);
    FOR_EACH(x){
        ret[i] = expandTokens(rpn(expr[i]), fenced, postfix); 
        if(!ret[i]) goto cleanup;
    }
    if (mode) reverse(r);
    switch (mode){
    case 0: r = joinStr(r, OP_POP); break;
    case 1: r = razeStr( (n == 1) ? r : joinObj(r, kc2(OP_ENLIST, n)) ); break;
    case 2: r = razeStr( joinObj(r, joinTag(f, OP_N_ARY + n)) ); break;
    }

    unref(fenced), unref(postfix);
    return UNREF_X(r);
cleanup:
    unref(x), unref(fenced), unref(postfix), unref(r);
    return 0;
}

// check that () and [] are balanced at all depth levels
// TODO: source level check on ()[]{} and source pointer in error message
static bool balanced(K tokens){
    uint64_t stk = 0;
    unsigned err = 0, sp = 0;
    K_char *s = CHR_PTR(tokens);
    FOR_EACH(tokens) {
        bool l = s[i]=='[', r = s[i]==']';
        bool o = (s[i]=='(')|l, c = (s[i]==')')|r;
        bool b = l|r;
        sp -= c;
        unsigned k = sp & 63;
        unsigned d = (unsigned)(((stk >> k) ^ b) & 1);
        err |= ((unsigned)sp >> 6) | ((unsigned)c & d);
        stk ^= (uint64_t)d << k;
        sp += o;
    }
    PARSE_ERROR(err|sp, -1, "unmatched", unref(tokens));
    return 1;
}

// compile source code to bytecode and vars/consts
// returns (bytecode; variables; constants; sourcecode)
K load(K src, K vars){
    K tokens, bytecode, consts = 0;
    tokens = token(src, &vars, &consts);
    if (!tokens || !balanced(tokens)) goto cleanup;
    bytecode = compile(0, tokens, 0);
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
    K_sym *v = SYM_PTR(vars);
    K_char *ip = CHR_PTR(x), *e = ip + HDR_COUNT(x);
    K stack[STACK_SIZE], *top = stack+STACK_SIZE, *base = top, a; // stack grows down
    while (ip < e){
        K_char i = *ip & 31; // index: lower 5 bits
        switch(*ip++ >> 5){  // class: upper 3 bits
        case 0: *top=monad_table[i](*top); if(!*top) goto bail; break;
        case 1: a=*top++; *top=dyad_table[i](a,*top); if (!*top) goto bail; break;
        case 2: a=*top++; *top=apply(a,i,top); unref(a); if (!*top) goto bail; break;
        case 3: *--top=ref(OBJ_PTR(consts)[i]); break;
        case 4: *--top=i<varc?ref(args[i]):getGlobal(GLOBALS,v[i]); if (!*top) goto bail; break;
        case 5: K*slot=i<varc?args+i:getSlot(GLOBALS,v[i]); unref(*slot); *slot=ref(*top); break;
        case 7: switch(i){ // special ops 0:pop 1:enlist
                case 0: unref(*top++); break;
                case 1: a=knew(KObjType,*ip++); FOR_EACH(a)OBJ_PTR(a)[i]=*top++; *--top=squeeze(a); break;
                default: NYI_ERROR(1, "vm: OP_SPECIAL", goto bail);
                } 
        }
    }
    return top == base ? knull() : *top;
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