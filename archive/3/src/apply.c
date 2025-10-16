#include "apply.h"
#include "object.h"
#include "verb.h"
#include "parse.h"
#include "compile.h"
#include "adverb.h"
#include "vm.h"
#include "io.h"

// forward declarations
K index(K x, K y);
K amend4(K*);

// count magic values 
static i8 countMV(K*x, i64 n){
    i8 cnt=0;
    for (i64 i=0; i<n; i++) cnt+=IS_MAGIC_VAL(x[i]);
    return cnt;
}

// f[;][1] -> f[1;]
static K fillMV(K x, K*y, i64 n){
    x=reuse(x);
    i64 j=1,xn=CNT(x);
    // iterate y
    for (i64 i=0; i<n; i++){
        // iterate the projection (start at 1st argument)
        for (; j<xn; j++){
            // fill magic values
            if (IS_MAGIC_VAL(OBJ(x)[j])){
                OBJ(x)[j++]=y[i];
                break;
            }
        }
    }
    HDR_RNK(x)-=n;
    return x;
}

// x . y
// generic apply function
// x - some applicable value (primitive, lambda, list, etc)
// y - a list of arguments to x
K apply(K x, K*y, i64 n){
    K r;
    i8  xt=TYP(x);

    // simple atom is not an applicable value
    // (some symbols are special, they have special functions)
    if (xt < 0 && xt != -KS){
        UNREF_N_OBJS(y,n);
        return UNREF_X(kerr("rank! atom not an applicable value"));
    }

    // index
    if (xt >= 0 && xt < K_INDEXABLE_END){
        // x[y]
        if (n == 1)
            return index(x,*y); 

        // x[y;...]
        for (i64 i=0; i<n; i++){
            // handle elided index. x[;i] -> x@\:i
            if (IS_MAGIC_VAL(y[i]))
                return (n == ++i) ? x : applyleft(x,&y[i],n-i);
            // apply to next index
            x=apply(x,&y[i],1);
            // handle error
            if (IS_ERROR(x)){
                ++i;
                UNREF_N_OBJS((&y[i]),n-i);
                break;
            }
        }
        return x;
    }

    // if any magic values, return projection
    i8 rn=countMV(y,n);
    if (rn){
        r=j2(k1(x),kn(y,n));
        HDR_RNK(r)=rn;
        return tx(KP,r);
    }

    // (,:;y)
    // enlist is special, can take any number of arguments
    if (IS_MONAD(x,TOK_COMMA)){
        return squeeze(kn(y,n));
    }

    i8 rank=rankOf(x);
    bool variadic=(rank < 0);
    if (variadic) rank=MIN(n,-rank);

    // too many args
    if (n > rank){
        UNREF_N_OBJS(y,n);
        return UNREF_X(kerr("'rank! too many args"));
    }

    // too few args
    if (n < rank){
        // create projection
        if (xt != KP){
            r=tx(KP,j2(k1(x),kn(y,n)));
            HDR_RNK(r)=rankOf(x)-n;
            return r;
        }
        // else fill in elided args of existing projection
        return fillMV(x,y,n);
    }

    // symbols - (`abc;args)
    // apply special functions
    if (xt == -KS){
        r=*y;
        switch(*INT(x)){
        // `p@"x+y" -> return parse tree
        case 'p':
            if (TYP(r) != KC)
                return UNREF_XR(kerr("'type! can only parse char vector"));
            r=j2(r,kc(0));
            return UNREF_XR(parse((const char*)r));

        // `x@"x+y" -> return bytecode
        case 'x':
            r=apply(ks('p'),&r,1);
            return UNREF_X((IS_ERROR(r)||IS_NULL(r)) ? r : compile(r));

        default :
            return UNREF_XR(kerr("'type! symbol not an applicable value"));
        }
    }

    // make adverb-modified functions + compositions
    if (xt == KW){
        return (n == 1) ? kwx(x,*y) : kq(*y,y[1]);
    }

    // apply composition
    // KQ objects are (f;g) so res=apply(g,y,n), if res!=error then apply(f,&res,1)
    if (xt == KQ){
        r=apply(ref(OBJ(x)[1]),y,n);
        return UNREF_X( IS_ERROR(r) ? r : apply(ref(*OBJ(x)),&r,1) );
    }

    // lambdas
    if (xt == KL){
        r=run(x,y,n);
        UNREF_N_OBJS(y,n);
        return r;
    }

    // projections
    if (xt == KP){
        x=countMV(&OBJ(x)[1],CNT(x)-1) ? fillMV(x,y,n) : j2(x,kn(y,n));
        y=&OBJ(x)[1];
        n=CNT(x)-1;
        ref(*OBJ(x));REF_N_OBJS(y,n);
        return UNREF_X(apply(OBJ(x)[0],y,n));
    }

    // f'x f/x ...
    if (IS_DERIVED_VERB(x)){
        K f=ref(*OBJ(x));

        // str\:y is special, eg " "\:"foo bar" -> ("foo";"bar")
        if (ABS(TYP(f)) == KC) return UNREF_X(splitString(f,*y));

        // else normal adverb 
        return (*adverb_table[xt-K_ADVERB_START])(UNREF_X(f),y,n);
    }

    if (rank == 4){
        return amend4(y);
    }

    // apply dyad
    if (xt == KV){
        return (*dyad_table[TAG_VAL(x)])(*y,y[1]);
    }
    // apply monad
    if (xt == KU){
        return (*monad_table[TAG_VAL(x)])(y[0]);
    }

    UNREF_N_OBJS(y,n);
    return UNREF_X(kerr("'nyi! apply"));
}

// get object with same shape, replacing all values with nulls
static K nulls(K x){
    if (!CNT(x)) return ref(x);

    K   r;
    i8  xt=TYP(x);
    i64 xn=CNT(x);

    // call recursively on generic objects
    if (!xt){
        r=tn(xt,xn);
        for (i64 i=0; i<xn; i++)
            OBJ(r)[i]=nulls(OBJ(x)[i]);
        return r;
    }

    // else create nulls
    switch(ABS(xt)){
    case KC: r=tn(xt,xn); for (i64 i=0; i<xn; i++) CHR(r)[i]=CNULL; return r;
    case KI: r=tn(xt,xn); for (i64 i=0; i<xn; i++) INT(r)[i]=INULL; return r;
    case KF: r=tn(xt,xn); for (i64 i=0; i<xn; i++) FLT(r)[i]=FNULL; return r;
    case KS: r=tn(xt,xn); for (i64 i=0; i<xn; i++) INT(r)[i]=SNULL; return r;
    case KD: { K k=ref(*OBJ(x)); return kD(k,nulls(OBJ(x)[1])); }
    case KT: return kT(nulls(*OBJ(x)));
    default: return ku(0);
    }
}

// index x at y
K index(K x, K y){
    // x[::] -> return x
    if (IS_MONAD(y,TOK_COLON) || IS_MAGIC_VAL(y))
        return x;
        
    // init
    K r,t;
    i8  xt=TYP(x), yt=TYP(y);
    i64 xn=CNT(x), yn=CNT(y);

    // handle dicts. indexed using find()
    if (xt==KD){
        // get the keys to search on
        K k=KEY(x);
        // find the indexes of the keys
        K ix=find(ref(k),y);
        // return if error finding keys
        if (IS_ERROR(ix))
            return UNREF_X(ix);
        // index the dict values
        return UNREF_X(index(ref(VAL(x)),ix));
    }

    // if y is general list, index for each item
    if (!yt){
        r=tn(KK,0);
        for (i64 i=0; i<yn; i++){
            // call on y[i]
            t=index(ref(x),ref(OBJ(y)[i]));
            // handle if error
            if (IS_ERROR(t))
                return UNREF_XYR(t);
            // else append to return object
            r=jk(r,t);
        }
        return UNREF_XY(squeeze(r));
    }

    // if y is a dict, index like (!y)!x[.y]
    if (yt==KD){
        r=index(ref(x),ref(VAL(y)));
        if (IS_ERROR(r))
            return UNREF_XY(r);
        return UNREF_XY(kD(ref(KEY(y)),r));
    }

    // handle table with sym index. this is treated as a dict index
    if (xt==KT && ABS(yt)==KS)
        return UNREF_X(index(ref(*OBJ(x)),y));

    // from here we're dealing with indexing with ints
    // so if y not int, return error
    if (ABS(yt)!=KI)
        return UNREF_XY(kerr("'type! y is not valid index"));

    // handle tables. call index for each column
    if (xt==KT){
        // extract the underlying dict
        K dict=*OBJ(x);
        K key=KEY(dict);
        K val=VAL(dict);
        K r=tn(KK,0);

        // iterate and index each column 
        for (i64 i=0,n=CNT(val); i<n; i++){
            t=index(ref(OBJ(val)[i]),ref(y));
            r=jk(r,t);
        }

        // make dict with new indices
        r=kD(ref(key),squeeze(r));
        // if y is atom, return dict. else return table
        return UNREF_XY(yt<0?r:kT(r));
    }

    // if x is generic and y is atom
    if (!xt && yt<0){
        // ()[0] -> ()
        if (!xn) return UNREF_Y(x);
        // return ref(x[*y]) or nulls if out of bounds
        i64 j=*INT(y);
        return UNREF_XY((j>=0&&j<xn) ? ref(OBJ(x)[j]) : nulls(*OBJ(x)));
    }
    
    // return object
    r=tn(yt>=0?xt:-xt,yn);

    i64 j;
    K nl=0;
    switch (xt){
    case KK: 
        for (i64 i=0; i<yn; i++){
            j=INT(y)[i];
            OBJ(r)[i]= (j>=0&&j<xn) ? ref(OBJ(x)[j]) : !nl?nl=nulls(xn?*OBJ(x):x):ref(nl);
        }
        r=squeeze(r);
        break;
    case KC: for (i64 i=0; i<yn; i++){ j=INT(y)[i], CHR(r)[i]=(j>=0&&j<xn)?CHR(x)[j]:CNULL; } break;
    case KI: for (i64 i=0; i<yn; i++){ j=INT(y)[i], INT(r)[i]=(j>=0&&j<xn)?INT(x)[j]:INULL; } break;
    case KF: for (i64 i=0; i<yn; i++){ j=INT(y)[i], FLT(r)[i]=(j>=0&&j<xn)?FLT(x)[j]:FNULL; } break;
    case KS: for (i64 i=0; i<yn; i++){ j=INT(y)[i], INT(r)[i]=(j>=0&&j<xn)?INT(x)[j]:SNULL; } break;
    }

    return UNREF_XY(r);
}

// @[r;i;f;y]
K amend4(K*x){
    // can't amend atom
    if (IS_ATOM(OBJ(x)[0])){
        UNREF_N_OBJS(x,4);
        return kerr("'rank! @[x;i;f;y] - x can't be atomic");
    }

    // unpack arg
    K  r=x[0];
    K ix=x[1];
    K  f=x[2];
    K  y=x[3];

    i64 rn=KCOUNT(r);

    // @[;::;;] -> @[;!#x;;]
    if (IS_MONAD(ix,TOK_COLON)){
        ix=TYP(r)==KD ? ref(KEY(r)) : til(rn);
        x[1]=ix;
    }

    // special case: f is ':' and i is atom 
    // if so, @[x;i;:;y] x[i]:(,y)
    // that is, x at index i is assigned all of y
    // @[1 2;0;:;3 7] -> (3 7;2)
    if (IS_DYAD(f,TOK_COLON) && IS_ATOM(ix) && !IS_ATOM(y)){
        y=enlist(y);
        x[3]=y;
    }

    i64 ixn=KCOUNT(ix);

    // i and y must have same count
    if (!IS_ATOM(y) && ixn!=KCOUNT(y)){
        UNREF_N_OBJS(x,4);
        return kerr("'length! amend @[x;i;f;y] - i and y must conform");
    }

    // if index is generic list, call recursively
    if (TYP(ix)==KK){
        for (i64 i=0; i<ixn; i++){
            // init new object
            K t[]={r,item(i,ix),ref(f),item(i,y)};
            r=amend4(t);
        }
        UNREF_N_OBJS((&x[1]),3);
        return r;
    }

    // amend dict
    if (TYP(r)==KD){
        K key=KEY(r);
        i64 kn=KCOUNT(key);
        K val=expand(VAL(r));

        // iterate each index and apply f[r i;y i]
        for (i64 i=0; i<ixn; i++){

            // get the key
            K keyind=find(ref(key),item(i,ix));
            if (IS_ERROR(keyind)){
                unref(val);
                VAL(r)=knul();
                UNREF_N_OBJS(x,4);
                return keyind;
            }
            i64 ind=INT(keyind)[0];
            unref(keyind);

            // amend
            // replace if key exists
            if (ind<kn){
                K t[]={ref(OBJ(val)[ind]),item(i,y)};
                K ret=apply(ref(f),t,2);
                if (IS_ERROR(ret)){
                    UNREF_N_OBJS(x,4);
                    return ret;
                }

                replace(&OBJ(val)[ind],ret);
            }
            // append if key doesn't exist
            else {
                key=j2(key,item(i,ix));
                val=jk(val,item(i,y));
                kn++;
            }
        }
        KEY(r)=key;
        VAL(r)=squeeze(val);
        UNREF_N_OBJS((&x[1]),3);
        return r;
    }
    // amend everything else
    else {
        r=expand(r);
        x[0]=r;

        // iterate each index and apply f[r i;y i]
        for (i64 i=0; i<ixn; i++){
            
            // get the index and check if it's out of bounds
            i64 ind=INT(ix)[IS_ATOM(ix)?0:i];
            if (ind<0 || ind>=rn){
                UNREF_N_OBJS(x,4);
                return kerr("'domain! @[x;i;f;y] - i out of bounds");
            }

            // f[r i;y i]
            K t[]={ref(OBJ(r)[ind]),item(i,y)};
            K ret=apply(ref(f),t,2);
            if (IS_ERROR(ret)){
                UNREF_N_OBJS(x,4);
                return ret;
            }

            replace(&OBJ(r)[ind],ret);
        }
        UNREF_N_OBJS((&x[1]),3);
        return squeeze(r);
    }
}
