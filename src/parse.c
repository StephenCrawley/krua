#include "parse.h"
#include "object.h"
#include "compile.h"

#define AT_EXPR_END(c)     sc(";)]}\0", (c))
#define COMPOSE(x,y)       k3(kw(0),x,y) 

// foward declarations
static K expr(Parser *p);
static K Exprs(char c, Parser *p);

// set error flag, return error object to be printed
// 'a' is flag to decide error msg to print. if >=0, it is an unexpected char in input stream
static K handleError(Parser *p, char a){
    // if already error, return KN object
    if (p->error) return knul();

    // else create error string to print later
    K r=kC0(a==-3 ? "'parse! invalid lambda args" :
            a==-2 ? "'parse! unclosed string"     :
            a==-1 ? "'parse! invalid number"      :
               !a ? "'parse! unexpected EOL"      :
                    "'parse! unexpected token: "  );
    if (a>0) r=j2(r,kc(a));

    // source with caret ^ pointing at char where error occurred
    // some errors we don't print this because we lose the info about where the error occurred
    if (a>-3){
        i64 n=p->current-p->src;
        r=k3(kC0((char*)p->src), j2((K)memset((void*)tn(KC,n),' ',n),kc('^')), r);
    }

    p->error=ke(r);
    return knul();
}

// increment current char pointer
static inline Parser *inc(Parser *p){ return ++p->current, p; }
// decrement current char pointer
static inline Parser *dec(Parser *p){ return --p->current, p; }

// consume whitepace
static inline void ws(Parser *p){ while(*p->current==' ') ++p->current; }

// peek char. consume whitespace and peek next char (return but don't consume it)
static inline char peek(Parser *p){ ws(p); char a=*p->current; return a=='/'&&p->current[-1]==' ' ? 0 : a; }

// next char. consume whitespace and consume & return next char
static inline char next(Parser *p){ char a=peek(p); return ++p->current, a; }

// return char class 
static char class(char c){
    //                           ! "#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~
    return (32>c||c>126) ? 0 : " +\"++++/()+++++/0000000000+ +++++aaaaaaaaaaaaaaaaaaaaaaaaaa / ++`aaaaaaaaaaaaaaaaaaaaaaaaaa{+}+"[c-32];
}

// parse ({[ Expressions... ]})
static K parseFenced(Parser *p, char b){
    char a=peek(p);
    // parse Expressions
    K r = ')'==a?tn(0,0):sc("}]",a)?kuc(':'):Exprs(')'==b?',':'}'==b?';':0, p);

    // if not properly closed, return error
    if (b != (a=peek(p)))
        return unref(r), handleError(p, a);

    return inc(p), r;
}

// parse x/
static K parseAdverb(Parser *p, K x){
    char t; //type
    do {
        t=iadverb(*p->current++);
        if (*p->current==':'){ ++p->current; t+=3; }
        // special case: if x==0 we're parsing bare adverbs (eg "'/")
        x= x ? k2(kw(t),x) : kw(t);
    } while (class(peek(p))=='/');
    return x;
}

// parse x[y;z]
static K parseMExpr(Parser *p, K x){
    K r;
    do {
        // parse [ ... ] and return if error. if empty [] (type KU) then box r
        r=parseFenced(inc(p),']');
        if (p->error)return UNREF_X(r);
        if (KU==TYP(r)) r = k1(r);
        // parse +[1] as (+:;1) and +[1;2] as (+;1;2)
        if (TYP(x)==KV && CNT(r)==1) x=tx(KU,x); 
        x = j2(k1(x),r);
    } while (peek(p)=='[');
    return x;
}

// parse adverbs and m-expressions
static K parsePostfix(Parser *p, K x){
    // parse m-expressions
    if (peek(p)=='['){
        p->verb=false; //f[] is a noun
        x=parseMExpr(p,x);
        if(p->error) return x;
    }

    // parse adverbs
    if (class(peek(p))=='/'){
        p->verb=true;
        x=parseAdverb(p,x);
    }

    // if followed by [, recurse
    return peek(p)=='[' ? parsePostfix(p,x) : x;
}

// parse "abc", "", etc
static K parseStr(Parser *p){
    const char *s = p->current;
    do { // if we hit EOL, error
        if (!*p->current) return handleError(p,-2);
    } while (*p->current++ != '"');
    i64 n = p->current-s-1;
    K r = tn(1 == n ? -KC : KC, n);
    memcpy(CHR(r), s, n);
    return r;
}

// parse 123
static i64 parseInt(const char *s, i32 len){
    if (*s=='-') return -parseInt(++s, len-1);
    i64 n = 0;
    for (i64 i=0; i<len; i++) n = (n*10) + (s[i]-'0'); 
    return n;
}

// parse 1.23
static double parseFlt(const char *s, i32 len, i32 d){
    if ('-' == *s) return -parseFlt(++s, len-1, d-1);
    double l, r; //left of dot, right of dot
    l = (double)(d       ? parseInt(s, d) : 0);
    r = (double)(d<len-1 ? parseInt(s+d+1, len-d-1) : 0);
    for (i32 i=0, n=len-d-1; i<n; i++) r/=10.0;
    return l + r;
}

static bool isNum(const char *s){
    char a=*s++;
    return class(a=='-'?s[*s=='.']:a=='.'?*s:a) == '0';
}

// parse number(s). "1","1 23"
static K parseNum(Parser *p){
    char a; //current
    const char *s=p->current;

    // first scan number/list of numbers
    // get num count (n) and determine if any are float (f)
    // also count number of decimals per num (d)
    i64 n=0,f=0,d=0;
    do {
        p->current+=(*p->current=='-');
        while (class(a=*p->current)=='0' || a=='.'){
            if ((d+=(a=='.'))>1) return handleError(p,-1);
            p->current++;
        }
        ++n, f|=d, d=0;
        if (*p->current!=' ') break;
        peek(p);
    } while (isNum(p->current));

    // rewind. create return object
    p->current=s;
    K r=tn((n>1?1:-1)*(f?KF:KI),n);

    // scan number(s) again and save into r
    for (i64 i=0,rn=n; i<rn; i++){
        n=peek(p)=='-',s=p->current,d=0;
        do d|=(s[n]=='.'), a=s[++n]; while (class(a)=='0' || a=='.');
        f ? FLT(r)[i]=parseFlt(s,n,d?ic((char*)s,'.'):n) : (INT(r)[i]=parseInt(s,n));
        p->current+=n;
    }
    return r;
}

static i64 encodeSym(Parser *p){
    char c;
    i64 n=0;
    const char *s=p->current;
    while ((c=class(*p->current))=='a' || c=='0') ++p->current;
    // encode char in i64. max 8 chars per symbol
    memcpy(&n,s,MIN(8,p->current-s));
    return n;
}

// parse `a`b`c
static K parseSym(Parser *p){
    K r = ks(encodeSym(inc(p)));
    while(peek(p)=='`') r=j2(r,ks(encodeSym(inc(p))));
    // enlist, as sym literals are enlisted in K parse tree
    return TYP(r)==KS ? k1(r) : enlist(r); 
}

static K parseVar(Parser *p){
    return ks(encodeSym(p));
}

// parse lambda args {[...] }. must be sym list
static K parseArgs(Parser *p){
    if (peek(p)==']') 
        return ++p->current, squeeze(k1(ks('x')));
    K r = squeeze(parseFenced(p,']'));
    if (p->error) return r;
    return KS==TYP(r) ? r : UNREF_R(handleError(p,-3));
}

static K classSwitch(Parser *p){
    char a, c; //current char, char class, 
    const char *s; // start of object
    K x, y; //x:parsed object, y:lambda args
    bool f=0; //is lambda function?

    a=next(p);
    // if "-" or "-." followed by digit, we're parsing a number, so classify appropriately
    c=isNum(p->current-1) ? '0' : class(a);

    switch (c){
    case '0': x=(a=='0'&&*p->current==':')?inc(p),kuc(a):parseNum(dec(p)); if (p->error)return x; break;
    case '`': x=parseSym(dec(p)); break;
    case 'a': x=parseVar(dec(p)); break;
    case '"': x=parseStr(p); break;
    case '+': x=kvc(a); a=peek(p); if(a==':'?inc(p),1:!AT_EXPR_END(a)&&a!='['&&(class(a)!='/'||a=='\'')) x=tx(KU,x); break;
    case '/': x=parseAdverb(dec(p),0); break;
    case '{': f=1,s=p->current-1,y=peek(p)=='['?parseArgs(inc(p)):squeeze(k1(ks(0))); if (p->error) return y; //no error->fallthrough
    // fall through
    case '(': x=parseFenced(p,")}"[f]); if (p->error){ if(f)unref(y); return x; } break;
    default : return handleError(p, a);
    }

    // set verb flag
    p->verb = (c == '+');

    // create lambda object
    if (f){
        y=k2(kCn((char*)s,p->current-s),y); //   ("{x}";args)
        x=compileLambda(x,y); // (bytecode;consts;"{x}";args)
        if (IS_ERROR(x)){
            p->error=x;
            return knul();
        }
    }

    return parsePostfix(p,x);
}

// parse single expression
static K expr(Parser *p){
    K x, y, z; //prefix, infix, right expression
    char a; //current char

    // parse prefix
    x=classSwitch(p);
    if (p->error) return x;

    // set composition flag and return x
    if (AT_EXPR_END(peek(p))) 
        return p->compose|=p->verb, x;

    // parse +x
    if (p->verb) 
        return y=expr(p), p->compose ? COMPOSE(x,y) : k2(x,y);

    // parse next term. some hairiness here
    // need to determine whether y is infix or start of another expression
    // if y is a verb ('*','+',"f/",etc) it is infix -> (y;x;expr())
    // else (y is a noun) it is the start of the next expr, so we rewind and parse (x;expr())
    // also, a '-' token can be dyadic minus or start of negative number
    // x-1 -> (-;`x;1) but x -1 -> (`x;-1)
    const char *temp=p->current; // so we can rewind if y is a noun
    bool isSpace=(p->current[-1]==' ');
    a=*p->current++;
    if (class(a)=='+' && (isSpace ? !isNum(p->current-1) : a!='.'||!isNum(p->current-1))){
        p->verb=true;
        y=parsePostfix(p,*p->current==':'?inc(p),kuc(a):kvc(a));
    }
    else {
        y=classSwitch(dec(p));
    }

    // if y is verb (x+) return (+;x). else return (x;y)
    if (AT_EXPR_END(peek(p)))
        return (p->compose|=p->verb) ? k2(y,x) : k2(x,y);

    // if y is a noun, rewind and return (x;expr())
    if (!p->verb){
        unref(y);
        p->current=temp;
        return y=expr(p), p->compose ? COMPOSE(x,y) : k2(x,y);
    }

    // parse x+y
    z=expr(p);
    return (p->compose && !IS_DYAD(y,TOK_COLON)) ? COMPOSE(k2(y,x),z) : k3(y,x,z);
}

// parse ;-delimited Expressions
static K Exprs(char c, Parser *p){
    K r=tn(0,0), t;

    do {
        t = AT_EXPR_END(peek(p))       ?  //if at expr end
            (c==';' ? kuc(':') : km()) :  //return null or magic value
            expr(p);                      //else parse expression
        r = jk(r,t);                      //join to previously parsed exprs
        p->compose = false;               //(re)set flags
    } while (next(p)==';');    
    --p->current;

    return !c ? r : CNT(r)==1 ? UNREF_R(ref(t)) : j2(k1(kuc(c)),r);
}

K parse(const char *src){
    K r;

    // init parser struct
    Parser p;
    p.compose = false;
    p.verb = false;
    p.error = false;
    p.src = src;
    p.current = src;

    // return if only whitespace or comment in input
    if ('/'==*src || sc("\n\0", peek(&p)))
        return ku(0);

    // parse Expressions
    r = Exprs(';', &p);
    // should be at EOL after calling Exprs()
    r = !peek(&p) ? r : UNREF_R(handleError(&p,*p.current));
    return p.error ? UNREF_R(p.error) : r;
}

#define SRC_MAX 128  //max repl source length

K readK(){
    // print prompt and read from stdin
    char src[SRC_MAX], *s;
    putchar(' ');
    if (!fgets(src, SRC_MAX, stdin))
        printf("'fgets\n;"), exit(1);

    // replace newline with 0
    if ((s=sc(src,'\n'))) *s='\0';

    // if not a system command, parse and return
    if (*(s=src) != '\\')
        return parse(s);

    if (!*++s || *s=='\\')
        exit(0);

    return kerr("'system! unknown command");
}
