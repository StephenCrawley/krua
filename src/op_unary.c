#include "op_unary.h"
#include "op_binary.h"
#include "object.h"
#include "apply.h"
#include "file.h"
#include "utils.h"
#include "error.h"

// iteration helpers over unary operators

K _each1(F1 f, K x){
    K r = knew(KObjType, HDR_COUNT(x));
    FOR_EACH(x){
        K t = f(item(i, x));
        if (!t) { HDR_COUNT(r)=i; unref(r); return UNREF_X(0); }
        OBJ_PTR(r)[i] = t;
    }
    return UNREF_X(r);
}


static K nyi1(K x){NYI_ERROR(1, "unary operator", unref(x);)}

//               :     +     -    *     %     &      |     <     >     =     @     .      !     ,     ?     #      _     ~     $     ^    csv
F1 unary_op[] = {nyi1, nyi1, neg, nyi1, nyi1, where, nyi1, nyi1, nyi1, nyi1, nyi1, value, til, nyi1, nyi1, count, nyi1, nyi1, nyi1, nyi1, csv};

// -x / neg x
K neg(K x){
    if (IS_TAG(x)){
        TYPE_ERROR(TAG_TYPE(x) != KIntType, "-x expects int", );
        return TAG(KIntType, -TAG_VAL(x));
    } else if (HDR_TYPE(x) == KObjType){
        return _each1(neg, x);
    } else if (HDR_TYPE(x) == KIntType){
        K r = reuse(KIntType, x);
        FOR_EACH(x) INT_PTR(r)[i] = -INT_PTR(x)[i];
        return UNREF_X(r);
    }
    TYPE_ERROR(1, "-x expects int", unref(x));
}

// &x / where x
K where(K x){
    TYPE_ERROR(IS_TAG(x) || HDR_TYPE(x) != KBoolType, "&x expects bool", unref(x));
    // over-read in both loops depends on zeroed last word beyond logical length n
    K_int n = 0, m = (HDR_COUNT(x)+7)/8;
    for (K_int i = 0; i < m; i++){
        n += stdc_count_ones(((uint64_t*)x)[i]);
    }
    K r = knew(KIntType, n);
    K_int idx = 0;
    for (K_int i = 0, o = 0; i < m; i++, o += 8){
        uint64_t word = ((uint64_t*)x)[i];
        // special-case packed word
        if (word == 0x0101010101010101UL){
            for (int j = 0; j < 8; j++) INT_PTR(r)[idx++] = o+j;
            continue;
        }
        while (word){
            INT_PTR(r)[idx++] = o + stdc_trailing_zeros(word)/8;
            word &= word - 1;
        }
    }
    return UNREF_X(r);
}

// .x / value x
K value(K x){
    TYPE_ERROR(TAG_TYPE(x) || HDR_TYPE(x) != KChrType, ". x", unref(x));
    return readFile(x);
}

// !x / til x
K til(K x){
    TYPE_ERROR(TAG_TYPE(x) != KIntType, "!x expects int atom", unref(x));
    K r = knew(KIntType, TAG_VAL(x));
    FOR_EACH(r) INT_PTR(r)[i] = i;
    return r;
}

// #x / count x
K count(K x){
    return UNREF_X(TAG(KIntType, IS_ATOM(x) ? 1 : HDR_COUNT(x)));
}

// 'csv' helper macro
// uses locals of 'csv'
#define PARSE_COL(KT, STORE, PARSE) do { \
      K col = knew(KT, rows); \
      for (K_int i = 0; i < rows; i++){ \
          K_int k = i*cn + j; \
          K_int start = h||k ? idx[k-1]+1 : 0; \
          STORE(col)[i] = PARSE(s + start, s + idx[k]); \
      } \
      OBJ_PTR(r)[rj++] = col; \
  } while(0)

static K_char chr4chr(K_char*s, K_char*e){ (void)e; return *s; }

K csv(K x){
    // first verify the argument
    TYPE_ERROR(IS_TAG(x) || HDR_TYPE(x) != KObjType || HDR_COUNT(x) != 3, 
        "csv expects 3-tuple (header(int); types(string); data(string))\nexample: csv (0; \"cii\"; \"path/to/file.csv\")", 
        unref(x));
    K *arg = OBJ_PTR(x);
    // header
    // h = arg[0] -> h = TAG_VAL(arg[0]) -> h = header names if h!=0
    K h = arg[0];
    TYPE_ERROR(TAG_TYPE(h) != KIntType, "x 0", unref(x));
    h = (K)TAG_VAL(h);
    // types
    K t = arg[1];
    TYPE_ERROR(TAG_TYPE(t) || HDR_TYPE(t) != KChrType || HDR_COUNT(t) == 0, "x 1", unref(x));
    K_int cn = HDR_COUNT(t); // csv column count
    K_int rn = cn; // return column count
    FOR_EACH(t){
        K_char c = CHR_PTR(t)[i];
        TYPE_ERROR(!strchr(" ci", c), "invalid csv col type", unref(x));
        rn -= c == ' '; // skip these cols
    }
    // csv data, and some numbers to help us along
    K d = arg[2];
    TYPE_ERROR(TAG_TYPE(d) || HDR_TYPE(d) != KChrType, "x 2", unref(x));
    d = readFile(ref(d));
    if (!d) return UNREF_X(0);
    K_char *s = CHR_PTR(d);
    // parse the header
    if (h){
        K_char *nl = (K_char*)memchr(s, '\n', HDR_COUNT(d));
        TYPE_ERROR(!nl, "newline", unref(x); unref(d));
        h = syms4chrs(cutStr(knewcopy(KChrType, (K_int)(nl - s), (K)s), ','));
        h = filter(1, h, eql(ref(t), kchr(' ')));
    }
    // create list indices of separator and newlines (cell ends)
    K sep = eql(kchr(','), ref(d));
    K nwl = eql(kchr('\n'),ref(d));
    K ind = eql(kint(1),max(sep, nwl));
    ind = where(ind);
    PARSE_ERROR(HDR_COUNT(ind) % cn != 0, -1,
        "malformed csv. sep+nl % rows != 0", 
        unref(x); unref(d); unref(h); unref(ind););
    // loop and parse
    K_int rows = HDR_COUNT(ind) / cn, *idx = INT_PTR(ind);
    if (h) rows -= 1, idx += cn;
    K r = knew(KObjType, rn);
    // outer loop columns
    K_int rj = 0; // used in PARSE_COL to advace return column count
    for (K_int j = 0; j < cn; j++){
        // inner strided loop column records
        switch (CHR_PTR(t)[j]){
        case ' ': /*skip this column*/ break;
        case 'c': PARSE_COL(KChrType, CHR_PTR, chr4chr); break;
        case 'i': PARSE_COL(KIntType, INT_PTR, int4chr); break;
        }
    }
    // cleanup
    unref(d), unref(x), unref(ind);
    // TODO: table type
    return h ? k2(h, r) : r;
}
