// krua test suite

#include "krua.h"
#include "eval.h"
#include "object.h"
#include "op_binary.h"
#include "error.h"

#ifdef TRACK_REFS
#include "refcount.h"
#endif

static int tests_run = 0;
static int tests_failed = 0;

#ifdef TRACK_REFS
// With tracking: automatic GLOBALS creation and leak checking
#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  %-40s", #name); \
    reset(); \
    GLOBALS = ksymdict(); \
    KEYWORDS = syms4chrs(cutStr(kcstr(KEYWORDS_STRING), ' ')); \
    test_##name(); \
    unref(KEYWORDS); \
    int leaks = check_leaks(GLOBALS); \
    unref(GLOBALS); \
    leaks += check_leaks((K)0); \
    if (leaks) { \
        printf("FAIL: %d leaks\n", leaks); \
        tests_failed++; \
    } else { \
        printf("PASS\n"); \
    } \
    tests_run++; \
} while(0)

#define PASS() return

#else
// Without tracking: original behavior
#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  %-40s", #name); \
    GLOBALS = ksymdict(); \
    KEYWORDS = syms4chrs(cutStr(kcstr(KEYWORDS_STRING), ' ')); \
    test_##name(); \
    unref(GLOBALS); \
    unref(KEYWORDS); \
    tests_run++; \
} while(0)

#define PASS() printf("PASS\n")

#endif

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); tests_failed++; return; } } while(0)

#define ASSERT_INT_ATOM(expr, expected) do { \
    K _r = eval(kcstr(expr)); \
    ASSERT(_r && IS_TAG(_r) && TAG_TYPE(_r) == KIntType, expr " should return int atom"); \
    ASSERT(TAG_VAL(_r) == (expected), expr " value mismatch"); \
} while(0)

#define ASSERT_INT_LIST(expr, n, vals) do { \
    K _r = eval(kcstr(expr)); \
    ASSERT(_r && !IS_TAG(_r) && HDR_TYPE(_r) == KIntType, expr " should return int list"); \
    ASSERT(HDR_COUNT(_r) == (n), expr " count mismatch"); \
    for (int _i = 0; _i < (n); _i++) \
        ASSERT(INT_PTR(_r)[_i] == (vals)[_i], expr " element mismatch"); \
    unref(_r); \
} while(0)

#define ASSERT_ERROR(expr, err) do { \
    K _r = eval(kcstr(expr)); \
    ASSERT(!_r, expr " should fail"); \
    ASSERT(kerrno == (err), expr " wrong error type"); \
} while(0)

#define ASSERT_2_INTS(col, a, b) do { \
    K _c = (col); \
    ASSERT(_c && !IS_TAG(_c) && HDR_TYPE(_c) == KIntType, #col " should be KIntType list"); \
    ASSERT(HDR_COUNT(_c) == 2, #col " should have 2 elements"); \
    ASSERT(INT_PTR(_c)[0] == (a), #col "[0] mismatch"); \
    ASSERT(INT_PTR(_c)[1] == (b), #col "[1] mismatch"); \
} while(0)

// fill the full bucket data area (clobbering positions past HDR_COUNT) so that
// vector reads past n see deterministic bytes — without this, garbage in bucket
// headroom makes the pre-zeroBoolTail result tail non-deterministic
#define FILL_BUCKET(x, v) memset((void*)(x), (v), BUCKET_SIZEOF(x) - HDR_PAD)

#define ASSERT_BOOL_TAIL_ZERO(r) do { \
    K_int _n = HDR_COUNT(r); \
    if (_n & 63) { \
        uint64_t _last = ((uint64_t*)(r))[(_n + 63) / 64 - 1]; \
        uint64_t _mask = (1ULL << (_n & 63)) - 1; \
        ASSERT((_last & ~_mask) == 0, "bool tail past n must be zero"); \
    } \
} while(0)

#define ASSERT_ALL_BITS_SET(r, n) do { \
    int _ok = 1; \
    for (K_int _b = 0; _b < (n); _b++) if (!GET_BIT(r, _b)) { _ok = 0; break; } \
    ASSERT(_ok, "all bits in valid range should be set"); \
} while(0)

/* Testing Practices:
 *
 * Test organization mirrors the interpreter pipeline:
 *   Preprocessing  → tests strip() directly on K strings
 *   Tokenization   → tests tokenize()/token() output (token streams, consts, vars)
 *   Compilation    → tests compile() bytecode output
 *   Runtime        → tests eval(kcstr("...")) — exercises full pipeline
 *
 * When adding or changing a feature, add tests at every pipeline stage it touches.
 * If token() is updated, add tokenize_* tests. If compile() changes, add compile_*
 * tests. If a new operator is added end-to-end, add tests at each stage: tokenization
 * (correct token output), compilation (correct bytecodes), and runtime (correct eval).
 *
 * Naming: prefix every test with its category, e.g. unary_neg_atom,
 * binary_add_obj_list, comparison_int_atom_true, tokenize_string_literal.
 *
 * Helpers:
 *   ASSERT_INT_ATOM(expr, expected)    — eval expr, check int tag with value
 *   ASSERT_INT_LIST(expr, n, vals)     — eval expr, check int list with elements
 *   ASSERT_ERROR(expr, KERR_*)         — eval expr, check failure + kerrno
 *   ASSERT_2_INTS(col, a, b)           — check K value is 2-elem int list [a,b]
 *   tokenize(src)                      — tokenize only, discard vars/consts
 *
 * General:
 * - Use kcstr(s) to create KChrType from null-terminated C strings
 * - Use IS_CLASS(class, byte) for bytecode range checks, not manual arithmetic
 * - Each test should clean up heap allocations with unref() (tags don't need unref)
 * - For lists: always assert HDR_COUNT before checking element values
 * - RUN_TEST macro handles GLOBALS/KEYWORDS creation automatically
 * - Call token() directly only when you need access to vars or consts
 * - Error tests: check kerrno after asserting !r — errors do not print
 */

// Test helpers
static K tokenize(const char *src) {
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K result = token(x, &vars, &consts);
    unref(x), unref(vars); unref(consts);
    return result;
}

// Verify K object is a valid lambda: (bytecode; params; consts; source)
static int is_valid_lambda(K x) {
    if (!x || IS_TAG(x)) return 0;
    if (HDR_TYPE(x) != KLambdaType) return 0;
    if (HDR_COUNT(x) != 4) return 0;
    // [0] bytecode (KChrType)
    if (HDR_TYPE(OBJ_PTR(x)[0]) != KChrType) return 0;
    // [1] params (KSymType)
    if (HDR_TYPE(OBJ_PTR(x)[1]) != KSymType) return 0;
    // [2] consts (KObjType or null)
    K consts = OBJ_PTR(x)[2];
    if (consts && !IS_TAG(consts) && HDR_TYPE(consts) != KObjType) return 0;
    // [3] source (KChrType)
    if (HDR_TYPE(OBJ_PTR(x)[3]) != KChrType) return 0;
    return 1;
}

// Preprocessing
TEST(preprocess_strip_leading_comment) {
    const char *src = "/ comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 0, "leading '/' should empty the string");
    unref(x);
    PASS();
}

TEST(preprocess_strip_trailing_comment) {
    const char *src = "1+2 / comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to '1+2'");
    ASSERT(memcmp(CHR_PTR(x), "1+2", 3) == 0, "content should be '1+2'");
    unref(x);
    PASS();
}

TEST(preprocess_strip_trailing_whitespace) {
    const char *src = "abc   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip whitespace");
    ASSERT(memcmp(CHR_PTR(x), "abc", 3) == 0, "content should be 'abc'");
    unref(x);
    PASS();
}

TEST(preprocess_strip_both) {
    const char *src = "x:1 / assign   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to 'x:1'");
    unref(x);
    PASS();
}

TEST(preprocess_only_whitespace) {
    K r = eval(kcstr("   "));
    ASSERT(r != 0, "whitespace-only string should be valid");
    ASSERT(r == knull(), "whitespace-only string should return K generic null");
    PASS();
}

/*TEST(ignore_quoted_slash){
    const char *src = "\"ignore / in quotes\"";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == strlen(src), "should not mistake quoted forward-slash for comment");
    unref(x);
    PASS();
}*/

// Tokenization: literals
TEST(tokenize_empty_input) {
    K r = tokenize("");
    ASSERT(r && HDR_COUNT(r) == 0, "empty should produce 0 tokens");
    unref(r);
    PASS();
}

TEST(tokenize_single_integer) {
    K r = tokenize("123");
    ASSERT(r && HDR_COUNT(r) == 1, "single int should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "int should be in CONST range");
    unref(r);
    PASS();
}

TEST(tokenize_integer_list) {
    K r = tokenize("123 456 789");
    ASSERT(r && HDR_COUNT(r) == 1, "int list should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "list should be in CONST range");
    unref(r);
    PASS();
}

TEST(tokenize_string_literal) {
    K r = tokenize("\"hello\"");
    ASSERT(r && HDR_COUNT(r) == 1, "string should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "string should be in CONST range");
    unref(r);
    PASS();
}

TEST(tokenize_char_literal) {
    K x = kcstr("\"a\"");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "single char string should produce 1 token");
    ASSERT(consts && IS_TAG(OBJ_PTR(consts)[0]), "single char should be a tag, not an array");
    ASSERT(TAG_TYPE(OBJ_PTR(consts)[0]) == KChrType, "should be KChrType");
    ASSERT(TAG_VAL(OBJ_PTR(consts)[0]) == 'a', "should be 'a'");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

TEST(tokenize_empty_string_literal) {
    K r = eval(kcstr("\"\""));
    ASSERT(r != 0, "empty string should be valid");
    ASSERT(HDR_TYPE(r) == KChrType, "should be KChrType");
    ASSERT(HDR_COUNT(r) == 0, "should have length 0");
    unref(r);
    PASS();
}

TEST(tokenize_trailing_whitespace) {
    K r = tokenize("123   ");
    ASSERT(r && HDR_COUNT(r) == 1, "should ignore trailing whitespace");
    unref(r);
    PASS();
}

TEST(tokenize_negative_atom) {
    K x = kcstr("-3");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "-3 should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "should be CONST");
    K c = OBJ_PTR(consts)[0];
    ASSERT(IS_TAG(c) && TAG_TYPE(c) == KIntType, "const should be an int atom");
    ASSERT(TAG_VAL(c) == -3, "const should be -3");
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

TEST(tokenize_negative_strand) {
    K x = kcstr("1 -2");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "1 -2 should produce 1 strand-literal token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "should be CONST");
    ASSERT_2_INTS(OBJ_PTR(consts)[0], 1, -2);
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

TEST(tokenize_negative_list) {
    K x = kcstr("-1 2 -3");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "-1 2 -3 should produce 1 token");
    K c = OBJ_PTR(consts)[0];
    ASSERT(!IS_TAG(c) && HDR_TYPE(c) == KIntType && HDR_COUNT(c) == 3, "const should be a 3-int list");
    ASSERT(INT_PTR(c)[0] == -1 && INT_PTR(c)[1] == 2 && INT_PTR(c)[2] == -3, "const should be -1 2 -3");
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

TEST(tokenize_negative_after_paren) {
    K x = kcstr("(-1 2)");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 3, "(-1 2) should produce 3 tokens");
    ASSERT(CHR_PTR(r)[0] == '(' && CHR_PTR(r)[2] == ')', "should be wrapped in ( )");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[1]), "middle should be CONST");
    ASSERT_2_INTS(OBJ_PTR(consts)[0], -1, 2);
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

TEST(tokenize_negative_trailing_space) {
    K x = kcstr("1 2 -3 ");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "trailing space should be trimmed -> 1 token");
    K c = OBJ_PTR(consts)[0];
    ASSERT(!IS_TAG(c) && HDR_TYPE(c) == KIntType && HDR_COUNT(c) == 3, "const should be a 3-int list");
    ASSERT(INT_PTR(c)[0] == 1 && INT_PTR(c)[1] == 2 && INT_PTR(c)[2] == -3, "const should be 1 2 -3");
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

// Tokenization: variables
TEST(tokenize_single_variable) {
    K r = tokenize("abc");
    ASSERT(r && HDR_COUNT(r) == 1, "single var should produce 1 token");
    ASSERT(IS_CLASS(OP_GET_VAR, CHR_PTR(r)[0]), "variable should be in GET_VAR range");
    unref(r);
    PASS();
}

TEST(tokenize_multiple_variables) {
    K r = tokenize("x y z");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 variable tokens");
    unref(r);
    PASS();
}

// Tokenization: operators
TEST(tokenize_binary_add) {
    K r = tokenize("123+456");
    ASSERT(r && HDR_COUNT(r) == 3, "binary op should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 1, "+ should be operator 1 (OPS[:+-*...])");
    unref(r);
    PASS();
}

TEST(tokenize_unary_plus) {
    K r = tokenize("+123");
    ASSERT(r && HDR_COUNT(r) == 2, "unary op should produce 2 tokens");
    ASSERT(CHR_PTR(r)[0] == 1, "+ should be operator 1");
    unref(r);
    PASS();
}

TEST(tokenize_assignment) {
    K r = tokenize("a:42");
    ASSERT(r && HDR_COUNT(r) == 3, "assignment should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 0, ": should be operator 0");
    unref(r);
    PASS();
}

TEST(tokenize_csv_keyword) {
    K r = tokenize("csv \"c,s,v\n1,2,3\"");
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 2, "should return token stream");
    ASSERT(CHR_PTR(r)[0] == 20, "should tokenize 'csv' keyword as operator with value 20");
    unref(r);
    PASS();
}

TEST(tokenize_subtraction) {
    K r = tokenize("1-2");
    ASSERT(r && HDR_COUNT(r) == 3, "1-2 should produce 3 tokens (no negative literal)");
    ASSERT(CHR_PTR(r)[1] == 2, "- should be operator 2 (OPS[:+-...])");
    unref(r);
    PASS();
}

TEST(tokenize_subtraction_after_paren) {
    K x = kcstr("(1 2)-3");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 5, "(1 2)-3 should produce 5 tokens");
    ASSERT(CHR_PTR(r)[3] == 2, "- should be operator 2, not part of a literal");
    ASSERT(consts && HDR_COUNT(consts) == 2, "should have 2 consts: (1 2) and 3");
    K c = OBJ_PTR(consts)[1];
    ASSERT(IS_TAG(c) && TAG_TYPE(c) == KIntType && TAG_VAL(c) == 3, "second const should be atom 3 (positive)");
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

// Tokenization: parens
TEST(tokenize_paren_passthrough) {
    K r = tokenize("(1)");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 tokens");
    ASSERT(CHR_PTR(r)[0] == '(', "first should be literal (");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[1]), "second should be CONST");
    ASSERT(CHR_PTR(r)[2] == ')', "third should be literal )");
    unref(r);
    PASS();
}

TEST(tokenize_empty_parens) {
    K vars = 0, consts = 0;
    K x = kcstr("()");
    K r = token(x, &vars, &consts);
    ASSERT(r && HDR_COUNT(r) == 1, "() should produce 1 token");
    ASSERT(CHR_PTR(r)[0] == OP_CONST, "should be CONST index 0");
    ASSERT(consts && HDR_COUNT(consts) == 1, "should have 1 constant");
    K c = OBJ_PTR(consts)[0];
    ASSERT(!IS_TAG(c) && HDR_TYPE(c) == KObjType && HDR_COUNT(c) == 0, "const should be empty list");
    unref(x), unref(r), unref(vars), unref(consts);
    PASS();
}

// Tokenization: lambdas
TEST(tokenize_lambda_simple) {
    K x = kcstr("{[x]x+1}");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r, "tokenization should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 1, "should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "list should be in CONST range");
    ASSERT(consts && HDR_COUNT(consts) == 1, "should add lambda to consts");
    ASSERT(HDR_TYPE(OBJ_PTR(consts)[0]) == KLambdaType, "const should be KLambdaType");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

TEST(tokenize_lambda_full_src) {
    const char *src = "{[x]x+1}";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && consts, "tokenization should succeed");
    K lambda = OBJ_PTR(consts)[0];
    ASSERT(is_valid_lambda(lambda), "should be valid lambda");
    K lambda_src = OBJ_PTR(lambda)[3];
    ASSERT((size_t)HDR_COUNT(lambda_src) == strlen(src), "source length should match");
    ASSERT(memcmp(CHR_PTR(lambda_src), src, strlen(src)) == 0, "should store full lambda source with braces and params");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

TEST(tokenize_lambda_nested) {
    const char *src = "{[x;y]y+{[a]a+1}x}";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && consts, "tokenization should succeed");
    K outer = OBJ_PTR(consts)[0];
    ASSERT(is_valid_lambda(outer), "outer should be valid lambda");
    ASSERT(HDR_COUNT(OBJ_PTR(outer)[1]) == 2, "outer should have 2 params");
    K outer_consts = OBJ_PTR(outer)[2];
    ASSERT(outer_consts && HDR_TYPE(outer_consts) == KObjType, "outer should have consts");
    int found_inner = 0;
    FOR_EACH(outer_consts) {
        K item = OBJ_PTR(outer_consts)[i];
        if (!IS_TAG(item) && HDR_TYPE(item) == KLambdaType) {
            ASSERT(is_valid_lambda(item), "inner should be valid lambda");
            ASSERT(HDR_COUNT(OBJ_PTR(item)[1]) == 1, "inner should have 1 param");
            found_inner = 1;
            break;
        }
    }
    ASSERT(found_inner, "should find inner lambda in outer consts");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

// Tokenization: errors
TEST(tokenize_error_invalid_token) {
    K r = tokenize("£");
    ASSERT(!r, "'£' should fail as invalid token");
    ASSERT(kerrno == KERR_PARSE, "invalid token should raise parse error");
    PASS();
}

TEST(tokenize_error_unclosed_string) {
    K r = tokenize("\"hello");
    ASSERT(!r, "unclosed string should fail");
    ASSERT(kerrno == KERR_PARSE, "unclosed string should raise parse error");
    PASS();
}

TEST(tokenize_error_unclosed_string_in_expr) {
    K r = tokenize("1+2+\"hello");
    ASSERT(!r, "unclosed string in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(tokenize_error_invalid_token_in_expr) {
    K r = tokenize("1+£+2");
    ASSERT(!r, "invalid token in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(tokenize_error_single_quote) {
    K r = tokenize("\"");
    ASSERT(!r, "single quote should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(tokenize_error_lambda_missing_bracket) {
    K r = eval(kcstr("{x}"));
    ASSERT(!r, "should error: lambda must have params");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(tokenize_error_lambda_unclosed_params) {
    K r = eval(kcstr("{[x"));
    ASSERT(!r, "should error: unclosed param list");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(tokenize_error_lambda_unclosed) {
    K r = eval(kcstr("{[x]x+1"));
    ASSERT(!r, "should error: unclosed lambda");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

// Compilation
TEST(compile_empty) {
    K tokens = tokenize("");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 0, "empty should compile to empty");
    unref(bytecode);
    PASS();
}

TEST(compile_constant) {
    K tokens = tokenize("42");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "constant should compile to 1 byte");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST instruction");
    unref(bytecode);
    PASS();
}

TEST(compile_binary_op) {
    K tokens = tokenize("1+2");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "binary op should compile to 3 bytes");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should be PUSH const");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 1), "third should be BINARY ADD");
    unref(bytecode);
    PASS();
}

TEST(compile_unary_op) {
    K tokens = tokenize("+5");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 2, "unary op should compile to 2 bytes");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(CHR_PTR(bytecode)[1] == (OP_UNARY + 1), "second should be UNARY ADD");
    unref(bytecode);
    PASS();
}

TEST(compile_variable) {
    K tokens = tokenize("x");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "variable should compile to 1 byte");
    ASSERT(IS_CLASS(OP_GET_VAR, CHR_PTR(bytecode)[0]), "should be GET_VAR instruction");
    unref(bytecode);
    PASS();
}

TEST(compile_assignment) {
    K vars = 0, consts = 0;
    K x = kcstr("a:42");
    K tokens = token(x, &vars, &consts);
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "assignment should compile");
    ASSERT(IS_CLASS(OP_SET_VAR, CHR_PTR(bytecode)[HDR_COUNT(bytecode)-1]), "last instruction should be SET_VAR class");
    unref(x), unref(bytecode); unref(vars); unref(consts);
    PASS();
}

TEST(compile_application) {
    K tokens = tokenize("f x");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "application should compile");
    ASSERT(IS_CLASS(OP_GET_VAR, CHR_PTR(bytecode)[0]), "first should be GET var");
    ASSERT(IS_CLASS(OP_GET_VAR, CHR_PTR(bytecode)[1]), "second should be GET var");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 10), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

TEST(compile_application2) {
    K tokens = tokenize("\"abc\" 0");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "application should compile");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should PUSH const");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 10), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

// Compilation: keywords
TEST(compile_keyword_unary) {
    // "neg 5" should compile identically to "- 5": [CONST, UNARY+2]
    K tokens = tokenize("neg 5");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 2, "keyword unary should compile to 2 bytes");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(CHR_PTR(bytecode)[1] == (OP_UNARY + 2), "second should be UNARY neg (op 2)");
    unref(bytecode);
    PASS();
}

TEST(compile_keyword_chain_unary) {
    // "#-!3" and "count neg til 3" should produce identical bytecode
    // → [CONST, UNARY+12, UNARY+2, UNARY+15]
    K tok1 = tokenize("#-!3"), tok2 = tokenize("count neg til 3");
    K bc1 = compile(0, tok1, 0), bc2 = compile(0, tok2, 0);
    ASSERT(bc1 && HDR_COUNT(bc1) == 4, "op chain should compile to 4 bytes");
    K_char *b = CHR_PTR(bc1);
    ASSERT(IS_CLASS(OP_CONST, b[0]), "first should be CONST");
    ASSERT(b[1] == OP_UNARY + 12, "second should be UNARY til (op 12)");
    ASSERT(b[2] == OP_UNARY + 2, "third should be UNARY neg (op 2)");
    ASSERT(b[3] == OP_UNARY + 15, "fourth should be UNARY count (op 15)");
    ASSERT(HDR_COUNT(bc1) == HDR_COUNT(bc2), "keyword form should have same length");
    ASSERT(memcmp(CHR_PTR(bc1), CHR_PTR(bc2), HDR_COUNT(bc1)) == 0, "keyword form should produce identical bytecode");
    unref(bc1), unref(bc2);
    PASS();
}

TEST(compile_csv_keyword) {
    K tokens = tokenize("csv \"c,s,v\n1,2,3\"");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "should compile without error");
    ASSERT(!IS_TAG(bytecode) && HDR_TYPE(bytecode) == KChrType && HDR_COUNT(bytecode) == 2, "should compile 2 bytecodes");
    ASSERT(CHR_PTR(bytecode)[1] == 20, "should compile 'csv' as a unary operator with value 20");
    unref(bytecode);
    PASS();
}

// Compilation: semicolons
TEST(compile_semicolon) {
    // "1;2" → [CONST, OP_POP, CONST]
    K tokens = tokenize("1;2");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "semicolon expr should compile to 3 bytes");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be CONST");
    ASSERT(CHR_PTR(bytecode)[1] == OP_POP, "second should be OP_POP");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[2]), "third should be CONST");
    unref(bytecode);
    PASS();
}

TEST(compile_fenced_semicolon) {
    // "(1;2)" → [CONST, CONST, OP_ENLIST, 2]
    K tokens = tokenize("(1;2)");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 4, "fenced semicolon should compile to 4 bytes");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be CONST");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should be CONST");
    ASSERT(CHR_PTR(bytecode)[2] == OP_ENLIST, "third should be OP_ENLIST");
    ASSERT(CHR_PTR(bytecode)[3] == 2, "fourth should be count 2");
    unref(bytecode);
    PASS();
}

// Compilation: parens
TEST(compile_paren_simple) {
    K tokens = tokenize("(1)");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "should compile to 1 byte");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(compile_paren_nested) {
    K tokens = tokenize("((1))");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "nested parens should compile to 1 byte");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(compile_paren_with_op) {
    K tokens = tokenize("(1+2)*3");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 5, "should compile to 5 bytes");
    ASSERT(CHR_PTR(bytecode)[4] == OP_BINARY + 3, "last should be BINARY *");
    unref(bytecode);
    PASS();
}

// Compilation: lambdas
TEST(compile_lambda_postfix_single_arg) {
    K x = kcstr("{[x]x+1}[6]");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && !IS_TAG(bytecode), "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 3, "bytecode should have 3 instructions");
    ASSERT(IS_CLASS(OP_CONST, bc[0]), "first should load lambda");
    ASSERT(IS_CLASS(OP_CONST, bc[1]), "second should load arg 6");
    ASSERT(bc[2] == OP_N_ARY + 1, "third should be N_ARY apply with 1 arg");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_lambda_postfix_two_args) {
    K x = kcstr("{[x;y]x+y}[1;6]");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 4, "bytecode should have 4 instructions");
    ASSERT(IS_CLASS(OP_CONST, bc[0]), "first should load lambda");
    ASSERT(IS_CLASS(OP_CONST, bc[1]), "second should load arg 1");
    ASSERT(IS_CLASS(OP_CONST, bc[2]), "third should load arg 6");
    ASSERT(bc[3] == OP_N_ARY + 2, "fourth should be N_ARY apply with 2 args");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

// Compilation: adverbs
TEST(compile_adverb_each_infix) {
    // x f'y → [load_y, load_x, load_f, OP_VERB+20, OP_N_ARY+2]
    K x = kcstr("x f'y");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 5, "should be 5 bytes");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[0]), "load y");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[1]), "load x");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[2]), "load f");
    ASSERT(bc[3] == OP_VERB + ADVERB_START, "each wrap");
    ASSERT(bc[4] == OP_N_ARY + 2, "apply 2");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_adverb_each_postfix_bracket) {
    // x f'[y] → [load_y, load_f, OP_VERB+20, OP_N_ARY+1, load_x, OP_BINARY+5]
    K x = kcstr("x f'[y]");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 6, "should be 6 bytes");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[0]), "load y");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[1]), "load f");
    ASSERT(bc[2] == OP_VERB + ADVERB_START, "each wrap");
    ASSERT(bc[3] == OP_N_ARY + 1, "apply 1");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[4]), "load x");
    ASSERT(bc[5] == OP_BINARY + 10, "binary apply");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_adverb_bare_op_unary) {
    // +'x → [load_x, OP_VERB+1, OP_VERB+20, OP_N_ARY+1]
    K x = kcstr("+'x");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 4, "should be 4 bytes");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[0]), "load x");
    ASSERT(bc[1] == OP_VERB + 1, "push + verb");
    ASSERT(bc[2] == OP_VERB + ADVERB_START, "each wrap");
    ASSERT(bc[3] == OP_N_ARY + 1, "apply 1");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_adverb_bare_op_infix) {
    // x+'y → [load_y, load_x, OP_VERB+1, OP_VERB+20, OP_N_ARY+2]
    K x = kcstr("x+'y");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 5, "should be 5 bytes");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[0]), "load y");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[1]), "load x");
    ASSERT(bc[2] == OP_VERB + 1, "push + verb");
    ASSERT(bc[3] == OP_VERB + ADVERB_START, "each wrap");
    ASSERT(bc[4] == OP_N_ARY + 2, "apply 2");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_adverb_bare_no_args) {
    // g:f' → [load_f, OP_VERB+20, OP_SET_VAR+g]
    K x = kcstr("g:f'");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 3, "should be 3 bytes");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[0]), "load f");
    ASSERT(bc[1] == OP_VERB + ADVERB_START, "each wrap");
    ASSERT(IS_CLASS(OP_SET_VAR, bc[2]), "set g");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(compile_bare_op_bracket) {
    // +[1;2] → [const_2, const_1, OP_VERB+1, OP_N_ARY+2]
    K x = kcstr("+[1;2]");
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");
    K_char *bc = CHR_PTR(bytecode);
    ASSERT(HDR_COUNT(bytecode) == 4, "should be 4 bytes");
    ASSERT(IS_CLASS(OP_CONST, bc[0]), "const 2");
    ASSERT(IS_CLASS(OP_CONST, bc[1]), "const 1");
    ASSERT(bc[2] == OP_VERB + 1, "push + verb");
    ASSERT(bc[3] == OP_N_ARY + 2, "apply 2");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

// Compilation: errors
TEST(compile_error_unmatched_paren) {
    K r = eval(kcstr("(1+2"));
    ASSERT(!r, "unmatched paren should error");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

// Runtime: unary ops
TEST(unary_neg_atom) {
    ASSERT_INT_ATOM("- 1", -1);
    PASS();
}

TEST(unary_neg_list) {
    ASSERT_INT_LIST("- 1 2 3", 3, ((K_int[]){-1, -2, -3}));
    PASS();
}

TEST(unary_neg_nested) {
    K r = eval(kcstr("-(1 2;3 4)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "-(1 2;3 4) should return obj list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(r0) && HDR_TYPE(r0) == KIntType && HDR_COUNT(r0) == 2, "first should be int list len 2");
    ASSERT(INT_PTR(r0)[0] == -1 && INT_PTR(r0)[1] == -2, "first should be -1 -2");
    ASSERT(!IS_TAG(r1) && HDR_TYPE(r1) == KIntType && HDR_COUNT(r1) == 2, "second should be int list len 2");
    ASSERT(INT_PTR(r1)[0] == -3 && INT_PTR(r1)[1] == -4, "second should be -3 -4");
    unref(r);
    PASS();
}

TEST(unary_keyword_neg_atom) {
    ASSERT_INT_ATOM("neg 1", -1);
    PASS();
}

TEST(unary_keyword_neg_list) {
    ASSERT_INT_LIST("neg 1 2 3", 3, ((K_int[]){-1, -2, -3}));
    PASS();
}

TEST(unary_neg_type_error) {
    ASSERT_ERROR("- \"abc\"", KERR_TYPE);
    PASS();
}

TEST(unary_first_atom) {
    ASSERT_INT_ATOM("*123", 123);
    PASS();
}

TEST(unary_first_list) {
    ASSERT_INT_ATOM("*123 1 2", 123);
    PASS();
}

TEST(unary_first_nested) {
    ASSERT_INT_LIST("*(1 2;3 4)", 2, ((K_int[]){1, 2}));
    PASS();
}

TEST(unary_keyword_first_atom) {
    ASSERT_INT_ATOM("first 123", 123);
    PASS();
}

TEST(unary_keyword_first_list) {
    ASSERT_INT_ATOM("first 123 1 2", 123);
    PASS();
}

TEST(unary_keyword_first_nested) {
    ASSERT_INT_LIST("first (1 2;3 4)", 2, ((K_int[]){1, 2}));
    PASS();
}

TEST(unary_til) {
    ASSERT_INT_LIST("!3", 3, ((K_int[]){0, 1, 2}));
    PASS();
}

TEST(unary_keyword_til) {
    ASSERT_INT_LIST("til 3", 3, ((K_int[]){0, 1, 2}));
    PASS();
}

TEST(unary_count_list) {
    ASSERT_INT_ATOM("#1 2 3", 3);
    PASS();
}

TEST(unary_count_atom) {
    ASSERT_INT_ATOM("#42", 1);
    PASS();
}

TEST(unary_keyword_count_list) {
    ASSERT_INT_ATOM("count 1 2 3", 3);
    PASS();
}

TEST(unary_keyword_count_atom) {
    ASSERT_INT_ATOM("count 42", 1);
    PASS();
}

TEST(unary_enlist_char) {
    K r = eval(kcstr(",\"a\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, ",\"a\" should return char list");
    ASSERT(HDR_COUNT(r) == 1, "result should have 1 element");
    ASSERT(CHR_PTR(r)[0] == 'a', "element should be 'a'");
    unref(r);
    PASS();
}

TEST(unary_enlist_int) {
    ASSERT_INT_LIST(",5", 1, ((K_int[]){5}));
    PASS();
}

TEST(unary_enlist_nested) {
    K r = eval(kcstr(",1 2 3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, ",1 2 3 should return obj list");
    ASSERT(HDR_COUNT(r) == 1, "result should have 1 element");
    K r0 = OBJ_PTR(r)[0];
    ASSERT(!IS_TAG(r0) && HDR_TYPE(r0) == KIntType && HDR_COUNT(r0) == 3, "element should be int list len 3");
    ASSERT(INT_PTR(r0)[0] == 1 && INT_PTR(r0)[1] == 2 && INT_PTR(r0)[2] == 3, "element should be 1 2 3");
    unref(r);
    PASS();
}

TEST(unary_value_basic) {
    K r = eval(kcstr(".\"tests/read.txt\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "should read file as KChrType");
    ASSERT(HDR_COUNT(r) == 5, "file should have 5 chars");
    ASSERT(memcmp(CHR_PTR(r), "hello", 5) == 0, "content should be 'hello'");
    unref(r);
    PASS();
}

TEST(unary_value_file_not_found) {
    K r = eval(kcstr(".\"nonexistent_file_12345.txt\""));
    ASSERT(!r, "missing file should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise KERR_VALUE");
    PASS();
}

TEST(unary_value_type_error) {
    K r = eval(kcstr(". 123"));
    ASSERT(!r, "value on integer should fail");
    ASSERT(kerrno == KERR_TYPE, "should raise KERR_TYPE");
    PASS();
}

TEST(unary_where_single) {
    ASSERT_INT_LIST("&1=!3", 1, ((K_int[]){1}));
    PASS();
}

TEST(unary_where_multiple) {
    ASSERT_INT_LIST("&1=0 1 2 1 0 0", 2, ((K_int[]){1, 3}));
    PASS();
}

// Runtime: not (~)
TEST(unary_not_int_atom) { // numeric atom -> bool, regardless of input type
    K r = eval(kcstr("~0"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KBoolType && TAG_VAL(r) == 1, "~0 -> 1b");
    r = eval(kcstr("~5"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KBoolType && TAG_VAL(r) == 0, "~5 -> 0b");
    PASS();
}
TEST(unary_not_char_atom) { // ~"a" -> 0b (atom result is bool, not char)
    K r = eval(kcstr("~\"a\""));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KBoolType && TAG_VAL(r) == 0, "~\"a\" -> 0b");
    PASS();
}
TEST(unary_not_int_list) { // ~0 5 0 -> 1 0 1b (eql path)
    K r = eval(kcstr("~0 5 0"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 3, "bool list len 3");
    ASSERT(GET_BIT(r,0)==1 && GET_BIT(r,1)==0 && GET_BIT(r,2)==1, "~0 5 0 -> 1 0 1");
    unref(r); PASS();
}
TEST(unary_not_bool_list) { // ~101b -> 010b (notBool fast path)
    K r = eval(kcstr("~(1=1 2 1)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 3, "bool list len 3");
    ASSERT(GET_BIT(r,0)==0 && GET_BIT(r,1)==1 && GET_BIT(r,2)==0, "~101b -> 010b");
    unref(r); PASS();
}
TEST(unary_not_obj_squeeze) { // ~(0;"a") -> 10b (mixed scalars squeeze to bool vector)
    K r = eval(kcstr("~(0;\"a\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 2, "bool list len 2");
    ASSERT(GET_BIT(r,0)==1 && GET_BIT(r,1)==0, "~(0;\"a\") -> 1 0");
    unref(r); PASS();
}
TEST(unary_not_obj_nested) { // ~(0 1;1 0) -> (10b;01b) (nested stays boxed; squeeze bails on non-atoms)
    K r = eval(kcstr("~(0 1;1 0)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 2, "obj list len 2");
    K a = OBJ_PTR(r)[0], b = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(a) && HDR_TYPE(a) == KBoolType && HDR_COUNT(a) == 2 && GET_BIT(a,0)==1 && GET_BIT(a,1)==0, "[0] -> 1 0b");
    ASSERT(!IS_TAG(b) && HDR_TYPE(b) == KBoolType && HDR_COUNT(b) == 2 && GET_BIT(b,0)==0 && GET_BIT(b,1)==1, "[1] -> 0 1b");
    unref(r); PASS();
}
TEST(unary_not_empty_int) { // ~0#0 -> empty bool list
    K r = eval(kcstr("~ 0#0"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 0, "empty bool");
    unref(r); PASS();
}
TEST(unary_not_empty_obj) { // ~() -> empty obj (generic empty stays generic)
    K r = eval(kcstr("~()"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 0, "empty obj");
    unref(r); PASS();
}
TEST(unary_not_op_type_error) { // ~+ -> type error (regression: tag fell through to HDR_TYPE and segfaulted)
    ASSERT_ERROR("~+", KERR_TYPE);
    PASS();
}
TEST(unary_not_lambda_type_error) { // ~lambda -> type error (non-numeric pointer)
    ASSERT_ERROR("~{[x]x}", KERR_TYPE);
    PASS();
}

// Runtime: csv
TEST(unary_csv_headerless_all_int) {
    K r = eval(kcstr("csv (0;\"ii\";\"tests/g.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType list");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 columns");
    ASSERT_2_INTS(OBJ_PTR(r)[0], 1, 7);
    ASSERT_2_INTS(OBJ_PTR(r)[1], 2, 11);
    unref(r);
    PASS();
}

TEST(unary_csv_headerless_skip_first) {
    K r = eval(kcstr("csv (0;\" i\";\"tests/g.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType list");
    ASSERT(HDR_COUNT(r) == 1, "should have 1 column");
    ASSERT_2_INTS(OBJ_PTR(r)[0], 2, 11);
    unref(r);
    PASS();
}

TEST(unary_csv_headerless_skip_last) {
    K r = eval(kcstr("csv (0;\"i \";\"tests/g.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType list");
    ASSERT(HDR_COUNT(r) == 1, "should have 1 column");
    ASSERT_2_INTS(OBJ_PTR(r)[0], 1, 7);
    unref(r);
    PASS();
}

TEST(unary_csv_header_full) {
    K r = eval(kcstr("csv (1;\"iicC\";\"tests/f.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType 2-tuple");
    ASSERT(HDR_COUNT(r) == 2, "tuple should have 2 elements (syms; cols)");
    K syms = OBJ_PTR(r)[0];
    ASSERT(!IS_TAG(syms) && HDR_TYPE(syms) == KSymType && HDR_COUNT(syms) == 4, "syms should be 4 KSymType");
    K cols = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(cols) && HDR_TYPE(cols) == KObjType && HDR_COUNT(cols) == 4, "cols should be 4 KObjType");
    ASSERT_2_INTS(OBJ_PTR(cols)[0], 1, 7);
    ASSERT_2_INTS(OBJ_PTR(cols)[1], 2, 11);
    K c2 = OBJ_PTR(cols)[2];
    ASSERT(!IS_TAG(c2) && HDR_TYPE(c2) == KChrType && HDR_COUNT(c2) == 2, "col 2 should be 2 KChrType");
    ASSERT(CHR_PTR(c2)[0] == 'a' && CHR_PTR(c2)[1] == 'b', "col 2 bytes should be 'a','b'");
    K c3 = OBJ_PTR(cols)[3];
    ASSERT(!IS_TAG(c3) && HDR_TYPE(c3) == KObjType && HDR_COUNT(c3) == 2, "col 3 should be 2-elem KObjType string list");
    K s0 = OBJ_PTR(c3)[0], s1 = OBJ_PTR(c3)[1];
    ASSERT(!IS_TAG(s0) && HDR_TYPE(s0) == KChrType && HDR_COUNT(s0) == 5 && memcmp(CHR_PTR(s0), "hello", 5) == 0, "col 3 elem 0 should be \"hello\"");
    ASSERT(!IS_TAG(s1) && HDR_TYPE(s1) == KChrType && HDR_COUNT(s1) == 5 && memcmp(CHR_PTR(s1), "world", 5) == 0, "col 3 elem 1 should be \"world\"");
    unref(r);
    PASS();
}

TEST(unary_csv_header_skip_middle) {
    K r = eval(kcstr("csv (1;\"i cC\";\"tests/f.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType 2-tuple");
    ASSERT(HDR_COUNT(r) == 2, "tuple should have 2 elements (syms; cols)");
    K cols = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(cols) && HDR_TYPE(cols) == KObjType && HDR_COUNT(cols) == 3, "cols should be 3 KObjType");
    ASSERT_2_INTS(OBJ_PTR(cols)[0], 1, 7);
    K c1 = OBJ_PTR(cols)[1];
    ASSERT(!IS_TAG(c1) && HDR_TYPE(c1) == KChrType && HDR_COUNT(c1) == 2, "col 1 should be 2 KChrType");
    ASSERT(CHR_PTR(c1)[0] == 'a' && CHR_PTR(c1)[1] == 'b', "col 1 bytes should be 'a','b'");
    K c2 = OBJ_PTR(cols)[2];
    ASSERT(!IS_TAG(c2) && HDR_TYPE(c2) == KObjType && HDR_COUNT(c2) == 2, "col 2 should be 2-elem KObjType string list");
    K s0 = OBJ_PTR(c2)[0], s1 = OBJ_PTR(c2)[1];
    ASSERT(!IS_TAG(s0) && HDR_TYPE(s0) == KChrType && HDR_COUNT(s0) == 5 && memcmp(CHR_PTR(s0), "hello", 5) == 0, "col 2 elem 0 should be \"hello\"");
    ASSERT(!IS_TAG(s1) && HDR_TYPE(s1) == KChrType && HDR_COUNT(s1) == 5 && memcmp(CHR_PTR(s1), "world", 5) == 0, "col 2 elem 1 should be \"world\"");
    unref(r);
    PASS();
}

TEST(unary_csv_arg_not_tuple_error) {
    ASSERT_ERROR("csv \"abc\"", KERR_TYPE);
    PASS();
}

TEST(unary_csv_arg_wrong_count_error) {
    ASSERT_ERROR("csv (0;\"ii\")", KERR_TYPE);
    PASS();
}

TEST(unary_csv_header_flag_not_int_error) {
    ASSERT_ERROR("csv (\"x\";\"ii\";\"tests/g.csv\")", KERR_TYPE);
    PASS();
}

TEST(unary_csv_types_not_string_error) {
    ASSERT_ERROR("csv (0;1;\"tests/g.csv\")", KERR_TYPE);
    PASS();
}

TEST(unary_csv_types_empty_error) {
    ASSERT_ERROR("csv (0;\"\";\"tests/g.csv\")", KERR_TYPE);
    PASS();
}

TEST(unary_csv_types_invalid_char_error) {
    ASSERT_ERROR("csv (0;\"z\";\"tests/g.csv\")", KERR_TYPE);
    PASS();
}

TEST(unary_csv_path_not_string_error) {
    ASSERT_ERROR("csv (0;\"ii\";42)", KERR_TYPE);
    PASS();
}

TEST(unary_csv_file_not_found_error) {
    ASSERT_ERROR("csv (0;\"ii\";\"nonexistent.csv\")", KERR_VALUE);
    PASS();
}

TEST(unary_csv_malformed_separators_error) {
    ASSERT_ERROR("csv (0;\"iii\";\"tests/g.csv\")", KERR_PARSE);
    PASS();
}

// Runtime: binary arithmetic
TEST(binary_add_atom) {
    ASSERT_INT_ATOM("1+2", 3);
    PASS();
}

TEST(binary_multiply_atom) {
    ASSERT_INT_ATOM("3*4", 12);
    PASS();
}

TEST(binary_sub_atom) {
    ASSERT_INT_ATOM("1-2", -1);
    PASS();
}

TEST(binary_sub_list_list) { // TODO: BINARY_OP doesn't handle list-list yet
    ASSERT_INT_LIST("1 2-3 4", 2, ((K_int[]){-2, -2}));
    PASS();
}

TEST(binary_sub_list_atom) {
    ASSERT_INT_LIST("1 2 3-1", 3, ((K_int[]){0, 1, 2}));
    PASS();
}

TEST(binary_add_obj_list) {
    K r = eval(kcstr("(1 2;3 4)+1 1"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1 2;3 4)+1 1 should return obj list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(r0) && HDR_TYPE(r0) == KIntType && HDR_COUNT(r0) == 2, "first element should be int list of length 2");
    ASSERT(INT_PTR(r0)[0] == 2 && INT_PTR(r0)[1] == 3, "first element should be 2 3");
    ASSERT(!IS_TAG(r1) && HDR_TYPE(r1) == KIntType && HDR_COUNT(r1) == 2, "second element should be int list of length 2");
    ASSERT(INT_PTR(r1)[0] == 4 && INT_PTR(r1)[1] == 5, "second element should be 4 5");
    unref(r);
    PASS();
}

TEST(binary_add_obj_obj) {
    K r = eval(kcstr("(1 2;3 4)+(1 2;3 4)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1 2;3 4)+(1 2;3 4) should return obj list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(r0) && HDR_TYPE(r0) == KIntType && HDR_COUNT(r0) == 2, "first element should be int list of length 2");
    ASSERT(INT_PTR(r0)[0] == 2 && INT_PTR(r0)[1] == 4, "first element should be 2 4");
    ASSERT(!IS_TAG(r1) && HDR_TYPE(r1) == KIntType && HDR_COUNT(r1) == 2, "second element should be int list of length 2");
    ASSERT(INT_PTR(r1)[0] == 6 && INT_PTR(r1)[1] == 8, "second element should be 6 8");
    unref(r);
    PASS();
}

TEST(binary_add_obj_atom) {
    K r = eval(kcstr("(1 2;3 4)+1"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1 2;3 4)+1 should return obj list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(r0) && HDR_TYPE(r0) == KIntType && HDR_COUNT(r0) == 2, "first element should be int list of length 2");
    ASSERT(INT_PTR(r0)[0] == 2 && INT_PTR(r0)[1] == 3, "first element should be 2 3");
    ASSERT(!IS_TAG(r1) && HDR_TYPE(r1) == KIntType && HDR_COUNT(r1) == 2, "second element should be int list of length 2");
    ASSERT(INT_PTR(r1)[0] == 4 && INT_PTR(r1)[1] == 5, "second element should be 4 5");
    unref(r);
    PASS();
}

// n=20 > VI lanes (8 AVX2 / 16 AVX512) — forces >1 vector iteration in LL/LA
TEST(binary_add_int_list_long) {
    K_int expected[20];
    for (int i = 0; i < 20; i++) expected[i] = 2 * i;
    ASSERT_INT_LIST("(!20)+!20", 20, expected);
    PASS();
}

TEST(binary_add_int_atom_long) {
    K_int expected[20];
    for (int i = 0; i < 20; i++) expected[i] = i + 5;
    ASSERT_INT_LIST("5+!20", 20, expected);
    PASS();
}

// Runtime: comparison
TEST(comparison_int_atom_true) {
    K r = eval(kcstr("1=1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KBoolType, "1=1 should return KBoolType atom");
    ASSERT(TAG_VAL(r) == 1, "1=1 should return 1b (True)");
    PASS();
}

TEST(comparison_int_atom_false) {
    K r = eval(kcstr("1=2"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KBoolType, "1=2 should return KBoolType atom");
    ASSERT(TAG_VAL(r) == 0, "1=2 should return 0b (False)");
    PASS();
}

TEST(comparison_int_atom_list) {
    K r = eval(kcstr("1=1 2 3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType, "1=1 2 3 returns KBoolType list");
    ASSERT(CHR_PTR(r)[0] == 1, "(1=1 2 3)[0] == 1");
    ASSERT(CHR_PTR(r)[1] == 0, "(1=1 2 3)[1] == 0");
    ASSERT(CHR_PTR(r)[2] == 0, "(1=1 2 3)[2] == 0");
    unref(r);
    PASS();
}

TEST(comparison_int_list_list) {
    K r = eval(kcstr("1 1=1 2"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType, "1 1=1 2 should return KBoolType list");
    ASSERT(CHR_PTR(r)[0] == 1, "(1 1=1 2)[0] == 1");
    ASSERT(CHR_PTR(r)[1] == 0, "(1 1=1 2)[1] == 0");
    unref(r);
    PASS();
}

// n=521 > 512 bits — exercises tail mask in the word containing bit n-1
// after multiple vector iterations. Sized to preempt AVX512 (512-bit lanes).
// FILL_BUCKET clobbers headroom so the pre-zeroBoolTail tail is forced high,
// making missing-mask bugs deterministic instead of probabilistic.

TEST(comparison_tail_int_eql_list) {
    K_int n = 521;
    K x = knew(KIntType, n), y = knew(KIntType, n);
    FILL_BUCKET(x, 0x42); FILL_BUCKET(y, 0x42);
    K r = eql(x, y);
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_int_eql_atom) {
    K_int n = 521;
    K x = knew(KIntType, n);
    FILL_BUCKET(x, 0x42);
    K r = eql(x, kint(0x42424242));
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_char_eql_list) {
    K_int n = 521;
    K x = knew(KChrType, n), y = knew(KChrType, n);
    FILL_BUCKET(x, 'B'); FILL_BUCKET(y, 'B');
    K r = eql(x, y);
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_char_eql_atom) {
    K_int n = 521;
    K x = knew(KChrType, n);
    FILL_BUCKET(x, 'B');
    K r = eql(x, kchr('B'));
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_and_list) {
    K_int n = 521;
    K x = knew(KBoolType, n), y = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF); FILL_BUCKET(y, 0xFF);
    K r = min(x, y);
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_and_atom) {
    K_int n = 521;
    K x = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF);
    K r = min(x, TAG(KBoolType, 1));
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_or_list) {
    K_int n = 521;
    K x = knew(KBoolType, n), y = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF); FILL_BUCKET(y, 0xFF);
    K r = max(x, y);
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_or_atom) {
    K_int n = 521;
    K x = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF);
    K r = max(x, TAG(KBoolType, 1));
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_eql_list) {
    K_int n = 521;
    K x = knew(KBoolType, n), y = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF); FILL_BUCKET(y, 0xFF);
    K r = eql(x, y);
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_tail_bool_eql_atom) {
    K_int n = 521;
    K x = knew(KBoolType, n);
    FILL_BUCKET(x, 0xFF);
    K r = eql(x, TAG(KBoolType, 1));
    ASSERT(r && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == n, "shape");
    ASSERT_ALL_BITS_SET(r, n);
    ASSERT_BOOL_TAIL_ZERO(r);
    unref(r);
    PASS();
}

TEST(comparison_min_atom) {
    ASSERT_INT_ATOM("1&2", 1);
    PASS();
}

TEST(comparison_min_atom_2) {
    ASSERT_INT_ATOM("2&1", 1);
    PASS();
}

TEST(comparison_max_atom) {
    ASSERT_INT_ATOM("1|2", 2);
    PASS();
}

TEST(comparison_max_atom_2) {
    ASSERT_INT_ATOM("2|1", 2);
    PASS();
}

TEST(comparison_min_list) {
    ASSERT_INT_LIST("1 & 1 0 2 3", 4, ((K_int[]){1, 0, 1, 1}));
    PASS();
}

TEST(comparison_max_list) {
    ASSERT_INT_LIST("1 | 1 0 2 3", 4, ((K_int[]){1, 1, 2, 3}));
    PASS();
}

// n past VI/VC lane width (8/32 AVX2, 16/64 AVX512) for VMIN kernels
TEST(comparison_min_int_list_long) {
    K_int expected[20];
    for (int i = 0; i < 20; i++) expected[i] = i < 10 ? i : 20 - i;
    ASSERT_INT_LIST("(!20)&(20-!20)", 20, expected);
    PASS();
}

TEST(comparison_min_char_list_long) {
    K r = eval(kcstr("\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"&\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\""));
    ASSERT(r && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 40, "shape");
    int ok = 1;
    for (K_int i = 0; i < 40; i++) if (CHR_PTR(r)[i] != 'A') { ok = 0; break; }
    ASSERT(ok, "min of all-A and all-B should be all-A");
    unref(r);
    PASS();
}

TEST(comparison_max_int_list_long) {
    K_int expected[20];
    for (int i = 0; i < 20; i++) expected[i] = i < 10 ? 20 - i : i;
    ASSERT_INT_LIST("(!20)|(20-!20)", 20, expected);
    PASS();
}

TEST(comparison_max_char_list_long) {
    K r = eval(kcstr("\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"|\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\""));
    ASSERT(r && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 40, "shape");
    int ok = 1;
    for (K_int i = 0; i < 40; i++) if (CHR_PTR(r)[i] != 'B') { ok = 0; break; }
    ASSERT(ok, "max of all-A and all-B should be all-B");
    unref(r);
    PASS();
}

TEST(comparison_min_bool) {
    K r = eval(kcstr("(1=1) & (1=1 2 3)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 3, "should return 3-element bool list");
    ASSERT(CHR_PTR(r)[0] == 1, "element 0 should be 1");
    ASSERT(CHR_PTR(r)[1] == 0, "element 1 should be 0");
    ASSERT(CHR_PTR(r)[2] == 0, "element 2 should be 0");
    unref(r);
    PASS();
}

TEST(comparison_max_bool) {
    K r = eval(kcstr("(1=1) | (1=1 2 3)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 3, "should return 3-element bool list");
    ASSERT(GET_BIT(r, 0) == 1, "element 0 should be 1");
    ASSERT(GET_BIT(r, 1) == 1, "element 1 should be 1");
    ASSERT(GET_BIT(r, 2) == 1, "element 2 should be 1");
    unref(r);
    PASS();
}

// Runtime: squeeze
// squeeze of a boxed KBoolType list routes through eql(x_as_long, TAG(KBoolType,1))
// via the 8-byte comparison kernel. Sizes 1/7/8/9/64/65 cover byte-internal tail mask,
// byte boundary, multi-chunk iteration, word boundary, and word-tail respectively.
// Headroom past n is poisoned with TAG(KBoolType,1) so a missing zeroBoolTail would
// leave 1-bits past position n (without poison, allocator-zero buckets hide the bug).
#define CHECK_SQUEEZE_BOOL(n_val) do { \
    K_int _n = (n_val); \
    K _x = knew(KObjType, _n); \
    K_int _cap = (BUCKET_SIZEOF(_x) - HDR_PAD) / 8; \
    for (K_int _k = 0; _k < _cap; _k++) OBJ_PTR(_x)[_k] = TAG(KBoolType, 1); \
    for (K_int _k = 0; _k < _n;   _k++) OBJ_PTR(_x)[_k] = TAG(KBoolType, _k & 1); \
    K _r = squeeze(_x); \
    ASSERT(_r && !IS_TAG(_r) && HDR_TYPE(_r) == KBoolType && HDR_COUNT(_r) == _n, "shape"); \
    int _ok = 1; \
    for (K_int _k = 0; _k < _n; _k++) if (GET_BIT(_r, _k) != (_k & 1)) { _ok = 0; break; } \
    ASSERT(_ok, "alternating bit pattern mismatch"); \
    ASSERT_BOOL_TAIL_ZERO(_r); \
    unref(_r); \
} while(0)

TEST(squeeze_bool_eval) {
    K r = eval(kcstr("(1=1;1=0;1=1)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType && HDR_COUNT(r) == 3, "shape");
    ASSERT(GET_BIT(r, 0) == 1, "bit 0 should be 1");
    ASSERT(GET_BIT(r, 1) == 0, "bit 1 should be 0");
    ASSERT(GET_BIT(r, 2) == 1, "bit 2 should be 1");
    unref(r);
    PASS();
}

TEST(squeeze_bool_n1)  { CHECK_SQUEEZE_BOOL(1);  PASS(); }
TEST(squeeze_bool_n7)  { CHECK_SQUEEZE_BOOL(7);  PASS(); }
TEST(squeeze_bool_n8)  { CHECK_SQUEEZE_BOOL(8);  PASS(); }
TEST(squeeze_bool_n9)  { CHECK_SQUEEZE_BOOL(9);  PASS(); }
TEST(squeeze_bool_n64) { CHECK_SQUEEZE_BOOL(64); PASS(); }
TEST(squeeze_bool_n65) { CHECK_SQUEEZE_BOOL(65); PASS(); }

// Runtime: assignment
TEST(assignment_basic) {
    K r = eval(kcstr("x:42"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym((K_char*)"x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 42, "x should be assigned 42");
    PASS();
}

TEST(assignment_basic_2) {
    K r = eval(kcstr("x:42+1"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym((K_char*)"x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 43, "x should be assigned 43");
    PASS();
}

TEST(assignment_undefined_variable) {
    K r = eval(kcstr("foo"));
    ASSERT(!r, "undefined variable should return error");
    ASSERT(kerrno == KERR_VALUE, "undefined variable should raise value error");
    PASS();
}

TEST(assignment_undefined_in_expr) {
    K r = eval(kcstr("1+foo+2"));
    ASSERT(!r, "undefined variable in expression should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise KERR_VALUE");
    PASS();
}

TEST(assignment_reassignment) {
    K r = eval(kcstr("v:9;v:\"a\";v"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KChrType && TAG_VAL(r) == 'a', "should return KChrType atom with value 'a'");
    PASS();
}

TEST(assignment_op) {
    ASSERT_INT_ATOM("f:+; f[1;6]", 7);
    PASS();
}

// Runtime: indexing
TEST(index_str_with_atom){
    K r = eval(kcstr("\"abc\" 0"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KChrType, "string index should return KChrType atom");
    ASSERT(TAG_VAL(r) == 'a', "string index should return 'a'");
    PASS();
}

TEST(index_str_with_list){
    K r = eval(kcstr("\"abc\" 2 1 0"));
    ASSERT(r && !IS_TAG(r), "indexing with list should return list");
    ASSERT(HDR_TYPE(r) == KChrType, "result should be KChrType");
    ASSERT(HDR_COUNT(r) == 3, "result should have length 3");
    ASSERT(memcmp(CHR_PTR(r), "cba", 3) == 0, "result should be \"cba\"");
    unref(r);
    PASS();
}

TEST(index_int_with_list){
    ASSERT_INT_ATOM("3 2 1@0", 3);
    PASS();
}

TEST(index_int_out_of_bounds){
    ASSERT_INT_ATOM("1 2@3", 0);
    PASS();
}

TEST(index_str_out_of_bounds){
    K r = eval(kcstr("\"ab\"@3"));
    ASSERT(r && IS_TAG(r), "out of bounds index should return atom");
    ASSERT(TAG_TYPE(r) == KChrType, "result should be KChrType");
    ASSERT(TAG_VAL(r) == ' ', "out of bounds string index should return space");
    PASS();
}

TEST(index_postfix_two_args){
    ASSERT_INT_ATOM("(1 2;3 4)[1;0]", 3);
    PASS();
}

TEST(index_postfix_three_args){
    ASSERT_INT_ATOM("((1 2;3 4);(5 6;7 8))[1;0;1]", 6);
    PASS();
}

TEST(index_postfix_single_arg){
    ASSERT_INT_ATOM("1 2 3[0]", 1);
    PASS();
}

TEST(apply_oob_multi_arg){
    K r = eval(kcstr("(1;\"ab\")[1;5]"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KChrType, "should be char");
    ASSERT(TAG_VAL(r) == ' ', "OOB string index should be space");
    PASS();
}

TEST(apply_atom_rank_error){
    K r = eval(kcstr("42[0]"));
    ASSERT(!r && kerrno == KERR_RANK, "atom apply should be rank error");
    PASS();
}

TEST(apply_cascade_rank_error){
    K r = eval(kcstr("1 2 3[0;0]"));
    ASSERT(!r && kerrno == KERR_RANK, "indexing atom should be rank error");
    PASS();
}

TEST(apply_string_cascade_rank_error){
    K r = eval(kcstr("\"abc\"[1;0]"));
    ASSERT(!r && kerrno == KERR_RANK, "indexing char atom should be rank error");
    PASS();
}

TEST(apply_too_many_args_rank_error){
    K r = eval(kcstr("(1 2;3 4)[0;0;0]"));
    ASSERT(!r && kerrno == KERR_RANK, "3 args on 2-deep should be rank error");
    PASS();
}

TEST(apply_chained_bracket_rank_error){
    K r = eval(kcstr("{[x]x+1}[2][0]"));
    ASSERT(!r && kerrno == KERR_RANK, "indexing lambda result atom should be rank error");
    PASS();
}

TEST(apply_keyword_dyadic_rank){ // applyOperator: keyword (csv, op>=20) is unary only
    ASSERT_ERROR("csv[1;2]", KERR_RANK);
    PASS();
}

TEST(apply_nested_nary_stack){ // VM N_ARY must pop all i consumed args; nested call must not read inner's stale slots
    ASSERT_INT_ATOM("+[+[1;2];10]", 13); // pre-fix read stale 2 -> 3+2=5
    ASSERT_INT_LIST("{[x;y;z]x+y+z}[{[x;y;z]x+y+z}[1 2 3;1;1];10;0]", 3, ((K_int[]){13,14,15})); // i=3: two stale slots, pre-fix gave 5 6 7
    PASS();
}

// Runtime: take (#)
TEST(binary_take_atom_int){
    ASSERT_INT_LIST("3#5", 3, ((K_int[]){5, 5, 5}));
    PASS();
}

TEST(binary_take_atom_char){
    K r = eval(kcstr("3#\"a\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "n#char-atom should be KChrType list");
    ASSERT(HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "aaa", 3) == 0, "3#\"a\" should be \"aaa\"");
    unref(r);
    PASS();
}

TEST(binary_take_undertake){
    ASSERT_INT_LIST("2#1 2 3 4", 2, ((K_int[]){1, 2}));
    PASS();
}

TEST(binary_take_exact){
    ASSERT_INT_LIST("3#1 2 3", 3, ((K_int[]){1, 2, 3}));
    PASS();
}

TEST(binary_take_overtake_cycle){
    ASSERT_INT_LIST("5#1 2 3", 5, ((K_int[]){1, 2, 3, 1, 2}));
    PASS();
}

TEST(binary_take_overtake_double){ // n > 2*count exercises the doubling recursion
    ASSERT_INT_LIST("7#1 2 3", 7, ((K_int[]){1, 2, 3, 1, 2, 3, 1}));
    PASS();
}

TEST(binary_take_overtake_boundary){ // n == 2*count
    ASSERT_INT_LIST("6#1 2 3", 6, ((K_int[]){1, 2, 3, 1, 2, 3}));
    PASS();
}

TEST(binary_take_overtake_char){
    K r = eval(kcstr("5#\"ab\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "overtake char should be KChrType list");
    ASSERT(HDR_COUNT(r) == 5 && memcmp(CHR_PTR(r), "ababa", 5) == 0, "5#\"ab\" should be \"ababa\"");
    unref(r);
    PASS();
}

TEST(binary_take_undertake_squeeze){ // shrink prefix turns homogeneous -> flat type
    ASSERT_INT_LIST("2#(1;2;\"ab\")", 2, ((K_int[]){1, 2}));
    PASS();
}

TEST(binary_take_undertake_stays_generic){ // heterogeneous prefix stays boxed
    K r = eval(kcstr("2#(\"ab\";1;2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "heterogeneous prefix stays generic");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 elements");
    unref(r);
    PASS();
}

TEST(binary_take_overtake_generic){ // n > 2*count: doubling path must ref boxed children
    K r = eval(kcstr("5#(1;\"ab\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "overtake stays generic (not squeezable)");
    ASSERT(HDR_COUNT(r) == 5, "should have 5 elements");
    ASSERT(IS_TAG(OBJ_PTR(r)[0]) && TAG_VAL(OBJ_PTR(r)[0]) == 1, "elem 0 is int 1");
    ASSERT(IS_TAG(OBJ_PTR(r)[4]) && TAG_VAL(OBJ_PTR(r)[4]) == 1, "elem 4 wraps to int 1");
    ASSERT(!IS_TAG(OBJ_PTR(r)[1]) && HDR_TYPE(OBJ_PTR(r)[1]) == KChrType, "boxed string preserved");
    unref(r);
    PASS();
}

TEST(binary_take_lambda_replicate){ // n#lambda: atom replicated into a generic list
    K r = eval(kcstr("3#{[x]x}"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "n#lambda should be a generic list");
    ASSERT(HDR_COUNT(r) == 3, "should have 3 elements");
    for (int i = 0; i < 3; i++)
        ASSERT(!IS_TAG(OBJ_PTR(r)[i]) && HDR_TYPE(OBJ_PTR(r)[i]) == KLambdaType, "each element is a lambda");
    unref(r);
    PASS();
}

TEST(binary_take_negative_atom){ // -x#atom replicates |x| times, sign ignored
    ASSERT_INT_LIST("-3#5", 3, ((K_int[]){5, 5, 5}));
    PASS();
}

TEST(binary_take_negative_atom_char){ // same for a char atom
    K r = eval(kcstr("-3#\"a\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "-x#char-atom should be KChrType list");
    ASSERT(HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "aaa", 3) == 0, "-3#\"a\" should be \"aaa\"");
    unref(r);
    PASS();
}

TEST(binary_take_negative_undertake){ // (-x)#y, |x|<count takes from the back
    ASSERT_INT_LIST("-2#1 2 3 4 5", 2, ((K_int[]){4, 5}));
    PASS();
}

TEST(binary_take_negative_single){ // last element
    ASSERT_INT_LIST("-1#1 2 3", 1, ((K_int[]){3}));
    PASS();
}

TEST(binary_take_negative_exact){ // |x|==count is the whole list
    ASSERT_INT_LIST("-5#1 2 3 4 5", 5, ((K_int[]){1, 2, 3, 4, 5}));
    PASS();
}

TEST(binary_take_negative_overtake_clamps){ // |x|>count does NOT cycle; it clamps to the whole list
    ASSERT_INT_LIST("-7#0 1 2 3 4", 5, ((K_int[]){0, 1, 2, 3, 4}));
    ASSERT_INT_LIST("-12#0 1 2 3 4", 5, ((K_int[]){0, 1, 2, 3, 4}));
    ASSERT_INT_LIST("-7#!5", 5, ((K_int[]){0, 1, 2, 3, 4}));
    PASS();
}

TEST(binary_take_negative_char){ // negative take on a char list (width 1)
    K r = eval(kcstr("-2#\"abcde\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "-x#char-list should be KChrType list");
    ASSERT(HDR_COUNT(r) == 2 && memcmp(CHR_PTR(r), "de", 2) == 0, "-2#\"abcde\" should be \"de\"");
    unref(r);
    PASS();
}

TEST(binary_take_negative_char_overtake){ // char overtake clamps to the whole list too
    K r = eval(kcstr("-7#\"abc\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "char overtake should be KChrType list");
    ASSERT(HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "-7#\"abc\" should clamp to \"abc\"");
    unref(r);
    PASS();
}

TEST(binary_take_negative_generic){ // negative take keeps boxed children and refs them
    K r = eval(kcstr("-2#(1;\"ab\";2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "negative take from generic stays generic");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 elements");
    ASSERT(!IS_TAG(OBJ_PTR(r)[0]) && HDR_TYPE(OBJ_PTR(r)[0]) == KChrType, "boxed string preserved");
    ASSERT(IS_TAG(OBJ_PTR(r)[1]) && TAG_VAL(OBJ_PTR(r)[1]) == 2, "trailing int preserved");
    unref(r);
    PASS();
}

TEST(binary_drop_front){ // x_y, x>0 drops from the front
    ASSERT_INT_LIST("2_1 2 3 4 5", 3, ((K_int[]){3, 4, 5}));
    PASS();
}

TEST(binary_drop_back){ // x_y, x<0 drops from the back
    ASSERT_INT_LIST("-2_1 2 3 4 5", 3, ((K_int[]){1, 2, 3}));
    PASS();
}

TEST(binary_drop_zero){ // 0_y is identity
    ASSERT_INT_LIST("0_1 2 3", 3, ((K_int[]){1, 2, 3}));
    PASS();
}

TEST(binary_drop_exact){ // dropping the whole list yields an empty, typed list
    ASSERT_INT_LIST("3_1 2 3", 0, ((K_int[]){0}));
    PASS();
}

TEST(binary_drop_overdrop){ // x >= count clamps to empty, type preserved
    ASSERT_INT_LIST("5_1 2 3", 0, ((K_int[]){0}));
    PASS();
}

TEST(binary_drop_overdrop_neg){ // -x >= count clamps to empty too
    ASSERT_INT_LIST("-5_1 2 3", 0, ((K_int[]){0}));
    PASS();
}

TEST(binary_drop_char){ // drop works on char lists (width 1)
    K r = eval(kcstr("2_\"abcde\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "x_char-list should be KChrType list");
    ASSERT(HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "cde", 3) == 0, "2_\"abcde\" should be \"cde\"");
    unref(r);
    PASS();
}

TEST(binary_drop_char_back){ // negative drop from the back of a char list
    K r = eval(kcstr("-2_\"abcde\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "-2_char-list should be KChrType list");
    ASSERT(HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "-2_\"abcde\" should be \"abc\"");
    unref(r);
    PASS();
}

TEST(binary_drop_generic){ // drop keeps boxed children and refs them
    K r = eval(kcstr("1_(1;\"ab\";2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "drop from generic stays generic");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 elements");
    ASSERT(!IS_TAG(OBJ_PTR(r)[0]) && HDR_TYPE(OBJ_PTR(r)[0]) == KChrType, "boxed string preserved");
    ASSERT(IS_TAG(OBJ_PTR(r)[1]) && TAG_VAL(OBJ_PTR(r)[1]) == 2, "trailing int preserved");
    unref(r);
    PASS();
}

TEST(binary_drop_generic_back){ // negative drop on a generic list copies from the front
    K r = eval(kcstr("-1_(1;\"ab\";2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "negative drop from generic stays generic");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 elements");
    ASSERT(IS_TAG(OBJ_PTR(r)[0]) && TAG_VAL(OBJ_PTR(r)[0]) == 1, "leading int preserved");
    ASSERT(!IS_TAG(OBJ_PTR(r)[1]) && HDR_TYPE(OBJ_PTR(r)[1]) == KChrType, "boxed string preserved");
    unref(r);
    PASS();
}

TEST(binary_drop_squeeze){ // shrinking a generic to one boxed element stays generic (not unboxed)
    K r = eval(kcstr("2_(1;2;\"ab\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "single boxed remainder stays generic");
    ASSERT(HDR_COUNT(r) == 1, "should have 1 element");
    ASSERT(!IS_TAG(OBJ_PTR(r)[0]) && HDR_TYPE(OBJ_PTR(r)[0]) == KChrType, "remaining element is the string");
    unref(r);
    PASS();
}

TEST(binary_drop_atom_y_type_error){ // y must be a list
    ASSERT_ERROR("1_5", KERR_TYPE);
    PASS();
}

TEST(binary_drop_nonint_x_type_error){ // x must be an int atom
    ASSERT_ERROR("\"x\"_1 2 3", KERR_TYPE);
    PASS();
}

// Runtime: cut (^)
TEST(binary_cut_char){ // x^y slices y at the sorted indices in x; last segment runs to #y
    K r = eval(kcstr("2 4^\"abcdefghi\"")); // -> ("cd";"efghi")
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "x^y should return a generic list");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 segments");
    K a = OBJ_PTR(r)[0], b = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(a) && HDR_TYPE(a) == KChrType && HDR_COUNT(a) == 2 && memcmp(CHR_PTR(a), "cd", 2) == 0, "seg 0 is \"cd\"");
    ASSERT(!IS_TAG(b) && HDR_TYPE(b) == KChrType && HDR_COUNT(b) == 5 && memcmp(CHR_PTR(b), "efghi", 5) == 0, "seg 1 runs to end: \"efghi\"");
    unref(r);
    PASS();
}

TEST(binary_cut_squeeze_boxed){ // homogeneous boxed segments squeeze back to flat lists
    K r = eval(kcstr("0 2 5^(1;3;\"a\";\"b\";\"c\";\"d\";\"e\")")); // -> (1 3;"abc";"de")
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return a generic list");
    ASSERT(HDR_COUNT(r) == 3, "should have 3 segments");
    K a = OBJ_PTR(r)[0], b = OBJ_PTR(r)[1], c = OBJ_PTR(r)[2];
    ASSERT(!IS_TAG(a) && HDR_TYPE(a) == KIntType && HDR_COUNT(a) == 2 && INT_PTR(a)[0] == 1 && INT_PTR(a)[1] == 3, "seg 0 squeezes to int list 1 3");
    ASSERT(!IS_TAG(b) && HDR_TYPE(b) == KChrType && HDR_COUNT(b) == 3 && memcmp(CHR_PTR(b), "abc", 3) == 0, "seg 1 squeezes to \"abc\"");
    ASSERT(!IS_TAG(c) && HDR_TYPE(c) == KChrType && HDR_COUNT(c) == 2 && memcmp(CHR_PTR(c), "de", 2) == 0, "seg 2 squeezes to \"de\"");
    unref(r);
    PASS();
}

TEST(binary_cut_empty_segments){ // repeated index -> empty segment; x[last]==#y -> empty tail
    K r = eval(kcstr("0 0 3^\"abc\"")); // -> ("";"abc";"")
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return a generic list");
    ASSERT(HDR_COUNT(r) == 3, "should have 3 segments");
    K a = OBJ_PTR(r)[0], b = OBJ_PTR(r)[1], c = OBJ_PTR(r)[2];
    ASSERT(!IS_TAG(a) && HDR_TYPE(a) == KChrType && HDR_COUNT(a) == 0, "seg 0 is empty");
    ASSERT(!IS_TAG(b) && HDR_TYPE(b) == KChrType && HDR_COUNT(b) == 3 && memcmp(CHR_PTR(b), "abc", 3) == 0, "seg 1 is \"abc\"");
    ASSERT(!IS_TAG(c) && HDR_TYPE(c) == KChrType && HDR_COUNT(c) == 0, "seg 2 is empty (x[last]==#y)");
    unref(r);
    PASS();
}

TEST(binary_cut_atom_x_type_error){ // x must be an int list, not an atom
    ASSERT_ERROR("2^\"abc\"", KERR_TYPE);
    PASS();
}

TEST(binary_cut_atom_y_type_error){ // y must be a list
    ASSERT_ERROR("0 1^5", KERR_RANK);
    PASS();
}

TEST(binary_cut_domain_error){ // x must be ordered, in domain 0..#y, and non-negative
    ASSERT_ERROR("4 2^\"abcdef\"", KERR_TYPE); // unordered (interior branch)
    ASSERT_ERROR("0 9^\"abc\"", KERR_TYPE);    // x[last] > #y (last branch; was a segfault)
    ASSERT_ERROR("-1 2^\"abc\"", KERR_TYPE);   // negative index
    PASS();
}

// Runtime: join (,)
TEST(binary_join_char_atoms){ // "a","b" -> "ab"
    K r = eval(kcstr("\"a\",\"b\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 2 && memcmp(CHR_PTR(r), "ab", 2) == 0, "should be \"ab\"");
    unref(r); PASS();
}
TEST(binary_join_char_list_atom){ // "ab","c" -> "abc"
    K r = eval(kcstr("\"ab\",\"c\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "should be \"abc\"");
    unref(r); PASS();
}
TEST(binary_join_char_atom_list){ // "a","bc" -> "abc"
    K r = eval(kcstr("\"a\",\"bc\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "should be \"abc\"");
    unref(r); PASS();
}
TEST(binary_join_int_atoms){ // 1,2 -> 1 2
    ASSERT_INT_LIST("1,2", 2, ((K_int[]){1, 2}));
    PASS();
}
TEST(binary_join_int_list_atom){ // 1 2,3 -> 1 2 3
    ASSERT_INT_LIST("1 2,3", 3, ((K_int[]){1, 2, 3}));
    PASS();
}
TEST(binary_join_int_atom_list){ // 1,2 3 -> 1 2 3
    ASSERT_INT_LIST("1,2 3", 3, ((K_int[]){1, 2, 3}));
    PASS();
}
TEST(binary_join_char_atom_obj){ // "a",(1;2 3) -> ("a";1;2 3)
    K r = eval(kcstr("\"a\",(1;2 3)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 3, "should be 3-elem obj list");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1], r2 = OBJ_PTR(r)[2];
    ASSERT(IS_TAG(r0) && TAG_TYPE(r0) == KChrType && TAG_VAL(r0) == 'a', "[0] char atom 'a'");
    ASSERT(IS_TAG(r1) && TAG_TYPE(r1) == KIntType && TAG_VAL(r1) == 1, "[1] int atom 1");
    ASSERT(!IS_TAG(r2) && HDR_TYPE(r2) == KIntType && HDR_COUNT(r2) == 2 && INT_PTR(r2)[0] == 2 && INT_PTR(r2)[1] == 3, "[2] int list 2 3");
    unref(r); PASS();
}
TEST(binary_join_obj_char_atom){ // (1;2 3),"a" -> (1;2 3;"a")
    K r = eval(kcstr("(1;2 3),\"a\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 3, "should be 3-elem obj list");
    K r0 = OBJ_PTR(r)[0], r1 = OBJ_PTR(r)[1], r2 = OBJ_PTR(r)[2];
    ASSERT(IS_TAG(r0) && TAG_TYPE(r0) == KIntType && TAG_VAL(r0) == 1, "[0] int atom 1");
    ASSERT(!IS_TAG(r1) && HDR_TYPE(r1) == KIntType && HDR_COUNT(r1) == 2 && INT_PTR(r1)[0] == 2 && INT_PTR(r1)[1] == 3, "[1] int list 2 3");
    ASSERT(IS_TAG(r2) && TAG_TYPE(r2) == KChrType && TAG_VAL(r2) == 'a', "[2] char atom 'a'");
    unref(r); PASS();
}
TEST(binary_join_atom_lambda){ // 5,{[a;b]a+b} -> (5; lambda)
    K r = eval(kcstr("5,{[a;b]a+b}"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 2, "should be 2-elem obj list");
    ASSERT(IS_TAG(OBJ_PTR(r)[0]) && TAG_TYPE(OBJ_PTR(r)[0]) == KIntType && TAG_VAL(OBJ_PTR(r)[0]) == 5, "[0] int atom 5");
    ASSERT(!IS_TAG(OBJ_PTR(r)[1]) && HDR_TYPE(OBJ_PTR(r)[1]) == KLambdaType, "[1] lambda");
    unref(r); PASS();
}
TEST(binary_join_lambda_atom){ // {[a;b]a+b},5 -> (lambda; 5)
    K r = eval(kcstr("{[a;b]a+b},5"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 2, "should be 2-elem obj list");
    ASSERT(!IS_TAG(OBJ_PTR(r)[0]) && HDR_TYPE(OBJ_PTR(r)[0]) == KLambdaType, "[0] lambda");
    ASSERT(IS_TAG(OBJ_PTR(r)[1]) && TAG_TYPE(OBJ_PTR(r)[1]) == KIntType && TAG_VAL(OBJ_PTR(r)[1]) == 5, "[1] int atom 5");
    unref(r); PASS();
}
TEST(binary_join_int_lists){ // 1 2,3 4 -> 1 2 3 4 (same-type joinList)
    ASSERT_INT_LIST("1 2,3 4", 4, ((K_int[]){1, 2, 3, 4}));
    PASS();
}
TEST(binary_join_char_lists){ // "ab","cd" -> "abcd" (same-type joinList)
    K r = eval(kcstr("\"ab\",\"cd\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 4 && memcmp(CHR_PTR(r), "abcd", 4) == 0, "should be \"abcd\"");
    unref(r); PASS();
}
TEST(binary_join_int_char_lists){ // 1 2,"ab" -> (1;2;"a";"b") (diff-type, both expanded)
    K r = eval(kcstr("1 2,\"ab\""));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType && HDR_COUNT(r) == 4, "should be 4-elem obj list");
    K *o = OBJ_PTR(r);
    ASSERT(IS_TAG(o[0]) && TAG_TYPE(o[0]) == KIntType && TAG_VAL(o[0]) == 1, "[0] int 1");
    ASSERT(IS_TAG(o[1]) && TAG_TYPE(o[1]) == KIntType && TAG_VAL(o[1]) == 2, "[1] int 2");
    ASSERT(IS_TAG(o[2]) && TAG_TYPE(o[2]) == KChrType && TAG_VAL(o[2]) == 'a', "[2] char 'a'");
    ASSERT(IS_TAG(o[3]) && TAG_TYPE(o[3]) == KChrType && TAG_VAL(o[3]) == 'b', "[3] char 'b'");
    unref(r); PASS();
}
TEST(binary_join_empty_diff_type){ // empty,diff-type list returns the non-empty operand unchanged (regression: pre-fix boxed to obj list)
    K r = eval(kcstr("(0#0),\"abc\"")); // empty int , char list -> "abc"
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "(0#0),\"abc\" should be \"abc\"");
    unref(r);
    r = eval(kcstr("\"abc\",(0#0)")); // char list , empty int -> "abc"
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 3 && memcmp(CHR_PTR(r), "abc", 3) == 0, "\"abc\",(0#0) should be \"abc\"");
    unref(r); PASS();
}

// Runtime: lambdas
TEST(lambda_eval_returns_lambda) {
    K r = eval(kcstr("{[x]x+1}"));
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    unref(r);
    PASS();
}

TEST(lambda_eval_no_params) {
    K r = eval(kcstr("{[]1}"));
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    unref(r);
    PASS();
}

TEST(lambda_eval_single_param) {
    K r = eval(kcstr("{[x]x}"));
    ASSERT(is_valid_lambda(r), "should be valid lambda");
    ASSERT(HDR_COUNT(OBJ_PTR(r)[1]) == 1, "should have 1 param");
    unref(r);
    PASS();
}

TEST(lambda_eval_multi_params) {
    K r = eval(kcstr("{[a;b;c]a+b+c}"));
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    ASSERT(HDR_COUNT(OBJ_PTR(r)[1]) == 3, "should have 3 params");
    unref(r);
    PASS();
}

TEST(lambda_apply_empty_body) {
    K r = eval(kcstr("{[x]}@42"));
    ASSERT(r == knull(), "lambda with empty body should return knull");
    PASS();
}

TEST(lambda_apply_identity) {
    ASSERT_INT_ATOM("{[x]x}@42", 42);
    PASS();
}

TEST(lambda_apply_add) {
    ASSERT_INT_ATOM("{[x]x+1}@2", 3);
    PASS();
}

TEST(lambda_apply_multiply) {
    ASSERT_INT_ATOM("{[x]x*2}@10", 20);
    PASS();
}

TEST(lambda_apply_with_local) {
    ASSERT_INT_ATOM("{[x]y:x+1}@6", 7);
    PASS();
}

TEST(lambda_apply_multiple_locals) {
    ASSERT_INT_ATOM("{[x]z:y:x+1}@6", 7);
    PASS();
}

TEST(lambda_apply_local_and_param) {
    ASSERT_INT_ATOM("{[x]x+y:x+1}@5", 11);
    PASS();
}

TEST(lambda_apply_nested) {
    ASSERT_INT_ATOM("{[x]x+{[y]y*2}@3}@5", 11);
    PASS();
}

TEST(lambda_apply_no_params) {
    K r = eval(kcstr("{[]42}"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "{[]42} should eval to lambda");
    unref(r);
    PASS();
}

TEST(lambda_postfix_eval){
    ASSERT_INT_ATOM("{[x]x+1}[6]", 7);
    PASS();
}

TEST(lambda_reassign_param) {
    K r = eval(kcstr("{[x]x:\"new\"}@\"old\""));
    // If double-free, likely crashes or fails leak test
    unref(r);
    PASS();
}

TEST(lambda_error) {
    // runtime error inside lambda is handled correctly
    K r = eval(kcstr("{[x]. x}@1"));
    ASSERT(!r, "error while evaluating lambda body returns error");
    ASSERT(kerrno == KERR_TYPE, "should raise type error");
    PASS();
}

TEST(lambda_error_undefined_var) {
    K r = eval(kcstr("{[x]x+z}@5"));
    ASSERT(!r, "referencing undefined variable in lambda body should return error");
    ASSERT(kerrno == KERR_VALUE, "should raise value error");
    PASS();
}

TEST(lambda_set_get) {
    ASSERT_INT_ATOM("{[x]a:x+1; a+2} 4", 7);
    PASS();
}

TEST(lambda_rank_error) { // applyLambda: argc < n
    ASSERT_ERROR("{[x]x}[1;2]", KERR_RANK);
    PASS();
}

// Runtime: parens / semicolons
TEST(paren_eval_simple) {
    ASSERT_INT_ATOM("(42)", 42);
    PASS();
}

TEST(paren_eval_grouping) {
    ASSERT_INT_ATOM("(1+2)*3", 9);
    PASS();
}

TEST(paren_eval_nested) {
    ASSERT_INT_ATOM("((1+2))", 3);
    PASS();
}

TEST(paren_eval_deep) {
    ASSERT_INT_ATOM("(((1+2)))", 3);
    PASS();
}

TEST(paren_eval_multiple) {
    ASSERT_INT_ATOM("(1+2)+(3+4)", 10);
    PASS();
}

TEST(semicolon_terminated_expr) {
    K r = eval(kcstr("1 2;"));
    ASSERT(r == knull(), "semicolon-terminated expression should return knull");
    PASS();
}

TEST(expr_multiexpr_basic) {
    ASSERT_INT_ATOM("1 2;2", 2);
    PASS();
}

TEST(expr_subexpr_with_ops) {
    ASSERT_INT_ATOM("1+2;3*4", 12);
    PASS();
}

TEST(expr_subexpr_assignment) {
    ASSERT_INT_ATOM("x:1;x+2", 3);
    PASS();
}

TEST(expr_fenced_subexpr_basic) {
    ASSERT_INT_LIST("(1;2)", 2, ((K_int[]){1, 2}));
    PASS();
}

TEST(expr_fenced_subexpr_with_ops) {
    ASSERT_INT_LIST("(1;2+3)", 2, ((K_int[]){1, 5}));
    PASS();
}

TEST(expr_fenced_subexpr_heterogeneous) {
    K r = eval(kcstr("(1;\"a\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1;\"a\") should return generic list");
    ASSERT(HDR_COUNT(r) == 2, "(1;\"a\") should have 2 elements");
    ASSERT(TAG_VAL(OBJ_PTR(r)[0]) == 1, "first element should be 1");
    ASSERT(TAG_VAL(OBJ_PTR(r)[1]) == 'a', "second element should be 'a'");
    unref(r);
    PASS();
}

// Runtime: adverbs
TEST(adverb_each_lambda_count) {
    ASSERT_INT_LIST("{[x]#x}'(1 2;3 4 5)", 2, ((K_int[]){2, 3}));
    PASS();
}

TEST(adverb_each_bare_op_count) {
    ASSERT_INT_LIST("#'(1 2;3 4 5)", 2, ((K_int[]){2, 3}));
    PASS();
}

TEST(adverb_each_atom_rank_error) {
    K r = eval(kcstr("#'1"));
    ASSERT(!r && kerrno == KERR_RANK, "each on atom should be rank error");
    PASS();
}

TEST(adverb_bare_op_bracket_eval) {
    ASSERT_INT_ATOM("+[1;2]", 3);
    PASS();
}

// each2: dyadic each (x f'y) — subtraction pins arg order (x f y, not y f x)
TEST(adverb_each2_list_list) { // 10 20-'1 2 -> 9 18
    ASSERT_INT_LIST("10 20-'1 2", 2, ((K_int[]){9, 18}));
    PASS();
}
TEST(adverb_each2_atom_left) { // 10-'1 2 3 -> 9 8 7 (eachright: atom broadcast over right)
    ASSERT_INT_LIST("10-'1 2 3", 3, ((K_int[]){9, 8, 7}));
    PASS();
}
TEST(adverb_each2_atom_right) { // 1 2 3-'10 -> -9 -8 -7 (eachleft: atom broadcast over left)
    ASSERT_INT_LIST("1 2 3-'10", 3, ((K_int[]){-9, -8, -7}));
    PASS();
}
TEST(adverb_each2_atom_atom_rank_error) { // 1+'2 -> rank error (via eachright IS_ATOM(y) guard)
    ASSERT_ERROR("1+'2", KERR_RANK);
    PASS();
}
TEST(adverb_each2_length_error) { // mismatched list lengths -> length error, both directions
    ASSERT_ERROR("2 3 4+'5 6", KERR_LENGTH);
    ASSERT_ERROR("2 3+'5 6 7", KERR_LENGTH);
    PASS();
}

// over1: fast paths (specialized +/ -/ */ kernels on KIntType)
TEST(adverb_over1_sum_fast) {
    ASSERT_INT_ATOM("+/1 2 3 4", 10);
    PASS();
}

TEST(adverb_over1_mul_fast) {
    ASSERT_INT_ATOM("*/1 2 3 4", 24);
    PASS();
}

TEST(adverb_over1_sub_fast) { // subOver's 2*x[0] trick, order-sensitive
    ASSERT_INT_ATOM("-/1 2 3 4", -8);
    PASS();
}

// over1: generic path (non-special ops fall through to over1Generic)
TEST(adverb_over1_max_generic) {
    ASSERT_INT_ATOM("|/3 1 4 1 5", 5);
    PASS();
}

TEST(adverb_over1_min_generic) {
    ASSERT_INT_ATOM("&/3 1 4 1 5", 1);
    PASS();
}

// over1: nested (generic, boxed children)
TEST(adverb_over1_nested) {
    ASSERT_INT_LIST("+/(1 2;3 4)", 2, ((K_int[]){4, 6}));
    PASS();
}

// over1: confirmed edge semantics
TEST(adverb_over1_empty_identity) { // empty KIntType -> specialized identity
    ASSERT_INT_ATOM("+/!0", 0);
    ASSERT_INT_ATOM("*/!0", 1);
    PASS();
}

TEST(adverb_over1_atom_rank_error) {
    ASSERT_ERROR("+/5", KERR_RANK);
    PASS();
}

// scan1: generic (scan has no fast path; all go through scan1Generic)
TEST(adverb_scan1_sum) {
    ASSERT_INT_LIST("+\\1 2 3 4", 4, ((K_int[]){1, 3, 6, 10}));
    PASS();
}

TEST(adverb_scan1_mul) {
    ASSERT_INT_LIST("*\\1 2 3 4", 4, ((K_int[]){1, 2, 6, 24}));
    PASS();
}

TEST(adverb_scan1_sub) {
    ASSERT_INT_LIST("-\\1 2 3 4", 4, ((K_int[]){1, -1, -4, -8}));
    PASS();
}

// scan1: nested (regression guard for the scan1Generic refcount fix)
TEST(adverb_scan1_nested) {
    K r = eval(kcstr("+\\(1 2;3 4)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "scan over nested should be generic list");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 rows");
    ASSERT_2_INTS(OBJ_PTR(r)[0], 1, 2); // corrupted before the refcount fix
    ASSERT_2_INTS(OBJ_PTR(r)[1], 4, 6);
    unref(r);
    PASS();
}

// each1 composed over over1/scan1 (adverb stacking)
TEST(adverb_each1_over1) {
    ASSERT_INT_LIST("+/'(1 2;3 4)", 2, ((K_int[]){3, 7}));
    PASS();
}

TEST(adverb_each1_scan1) {
    K r = eval(kcstr("+\\'(1 2;3 4)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "each-scan should be generic list");
    ASSERT(HDR_COUNT(r) == 2, "should have 2 rows");
    ASSERT_2_INTS(OBJ_PTR(r)[0], 1, 3);
    ASSERT_2_INTS(OBJ_PTR(r)[1], 3, 7);
    unref(r);
    PASS();
}

// adverb error/cleanup paths: inner apply fails mid-reduce/scan/each (partial-result cleanup)
TEST(adverb_over1_reduce_length_error) { // over1Generic: unref(x) on propagated error
    ASSERT_ERROR("+/(1 2;3 4 5)", KERR_LENGTH);
    PASS();
}

TEST(adverb_scan1_reduce_length_error) { // scan1Generic: HDR_COUNT(r)=i; unref(r) partial cleanup
    ASSERT_ERROR("+\\(1 2;3 4 5)", KERR_LENGTH);
    PASS();
}

TEST(adverb_each1_inner_rank_error) { // each1Generic: 2nd element (atom) fails inner over, partial cleanup
    ASSERT_ERROR("+/'(1 2;3)", KERR_RANK);
    PASS();
}

// Runtime: \t timing
TEST(timeexpr_basic) {
    K r = eval(kcstr("\\t 1+1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType, "\\t expr should return int millis");
    PASS();
}

TEST(timeexpr_iterations) {
    K r = eval(kcstr("\\t:10 1+1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType, "\\t:N expr should return int millis");
    PASS();
}

TEST(timeexpr_error_no_space) {
    ASSERT_ERROR("\\t:10", KERR_PARSE);
    PASS();
}

TEST(timeexpr_error_bad_expr) { // load-failure path: timeExpr must unref x, not just the kstr copy
    ASSERT_ERROR("\\t \"abc", KERR_PARSE);
    PASS();
}

void run_tests() {
    printf("\nPreprocessing:\n");
    RUN_TEST(preprocess_strip_leading_comment);
    RUN_TEST(preprocess_strip_trailing_comment);
    RUN_TEST(preprocess_strip_trailing_whitespace);
    RUN_TEST(preprocess_strip_both);
    RUN_TEST(preprocess_only_whitespace);
    //RUN_TEST(ignore_quoted_slash);

    printf("\nTokenization:\n");
    // literals
    RUN_TEST(tokenize_empty_input);
    RUN_TEST(tokenize_single_integer);
    RUN_TEST(tokenize_integer_list);
    RUN_TEST(tokenize_string_literal);
    RUN_TEST(tokenize_char_literal);
    RUN_TEST(tokenize_empty_string_literal);
    RUN_TEST(tokenize_trailing_whitespace);
    RUN_TEST(tokenize_negative_atom);
    RUN_TEST(tokenize_negative_strand);
    RUN_TEST(tokenize_negative_list);
    RUN_TEST(tokenize_negative_after_paren);
    RUN_TEST(tokenize_negative_trailing_space);
    // variables
    RUN_TEST(tokenize_single_variable);
    RUN_TEST(tokenize_multiple_variables);
    // operators
    RUN_TEST(tokenize_binary_add);
    RUN_TEST(tokenize_unary_plus);
    RUN_TEST(tokenize_assignment);
    RUN_TEST(tokenize_csv_keyword);
    RUN_TEST(tokenize_subtraction);
    RUN_TEST(tokenize_subtraction_after_paren);
    // parens
    RUN_TEST(tokenize_paren_passthrough);
    RUN_TEST(tokenize_empty_parens);
    // lambdas
    RUN_TEST(tokenize_lambda_simple);
    RUN_TEST(tokenize_lambda_full_src);
    RUN_TEST(tokenize_lambda_nested);
    // errors
    RUN_TEST(tokenize_error_invalid_token);
    RUN_TEST(tokenize_error_unclosed_string);
    RUN_TEST(tokenize_error_unclosed_string_in_expr);
    RUN_TEST(tokenize_error_invalid_token_in_expr);
    RUN_TEST(tokenize_error_single_quote);
    RUN_TEST(tokenize_error_lambda_missing_bracket);
    RUN_TEST(tokenize_error_lambda_unclosed_params);
    RUN_TEST(tokenize_error_lambda_unclosed);

    printf("\nCompilation:\n");
    RUN_TEST(compile_empty);
    RUN_TEST(compile_constant);
    RUN_TEST(compile_binary_op);
    RUN_TEST(compile_unary_op);
    RUN_TEST(compile_variable);
    RUN_TEST(compile_assignment);
    RUN_TEST(compile_application);
    RUN_TEST(compile_application2);
    // keywords
    RUN_TEST(compile_keyword_unary);
    RUN_TEST(compile_keyword_chain_unary);
    RUN_TEST(compile_csv_keyword);
    // semicolons
    RUN_TEST(compile_semicolon);
    RUN_TEST(compile_fenced_semicolon);
    // parens
    RUN_TEST(compile_paren_simple);
    RUN_TEST(compile_paren_nested);
    RUN_TEST(compile_paren_with_op);
    // lambdas
    RUN_TEST(compile_lambda_postfix_single_arg);
    RUN_TEST(compile_lambda_postfix_two_args);
    // adverbs
    RUN_TEST(compile_adverb_each_infix);
    RUN_TEST(compile_adverb_each_postfix_bracket);
    RUN_TEST(compile_adverb_bare_op_unary);
    RUN_TEST(compile_adverb_bare_op_infix);
    RUN_TEST(compile_adverb_bare_no_args);
    RUN_TEST(compile_bare_op_bracket);
    // errors
    RUN_TEST(compile_error_unmatched_paren);

    printf("\nRuntime:\n");
    // unary ops
    RUN_TEST(unary_neg_atom);
    RUN_TEST(unary_neg_list);
    RUN_TEST(unary_neg_nested);
    RUN_TEST(unary_keyword_neg_atom);
    RUN_TEST(unary_keyword_neg_list);
    RUN_TEST(unary_neg_type_error);
    RUN_TEST(unary_first_atom);
    RUN_TEST(unary_first_list);
    RUN_TEST(unary_first_nested);
    RUN_TEST(unary_keyword_first_atom);
    RUN_TEST(unary_keyword_first_list);
    RUN_TEST(unary_keyword_first_nested);
    RUN_TEST(unary_til);
    RUN_TEST(unary_keyword_til);
    RUN_TEST(unary_count_list);
    RUN_TEST(unary_count_atom);
    RUN_TEST(unary_keyword_count_list);
    RUN_TEST(unary_keyword_count_atom);
    RUN_TEST(unary_enlist_char);
    RUN_TEST(unary_enlist_int);
    RUN_TEST(unary_enlist_nested);
    RUN_TEST(unary_value_basic);
    RUN_TEST(unary_value_file_not_found);
    RUN_TEST(unary_value_type_error);
    RUN_TEST(unary_where_single);
    RUN_TEST(unary_where_multiple);
    RUN_TEST(unary_not_int_atom);
    RUN_TEST(unary_not_char_atom);
    RUN_TEST(unary_not_int_list);
    RUN_TEST(unary_not_bool_list);
    RUN_TEST(unary_not_obj_squeeze);
    RUN_TEST(unary_not_obj_nested);
    RUN_TEST(unary_not_empty_int);
    RUN_TEST(unary_not_empty_obj);
    RUN_TEST(unary_not_op_type_error);
    RUN_TEST(unary_not_lambda_type_error);
    // csv
    RUN_TEST(unary_csv_headerless_all_int);
    RUN_TEST(unary_csv_headerless_skip_first);
    RUN_TEST(unary_csv_headerless_skip_last);
    RUN_TEST(unary_csv_header_full);
    RUN_TEST(unary_csv_header_skip_middle);
    RUN_TEST(unary_csv_arg_not_tuple_error);
    RUN_TEST(unary_csv_arg_wrong_count_error);
    RUN_TEST(unary_csv_header_flag_not_int_error);
    RUN_TEST(unary_csv_types_not_string_error);
    RUN_TEST(unary_csv_types_empty_error);
    RUN_TEST(unary_csv_types_invalid_char_error);
    RUN_TEST(unary_csv_path_not_string_error);
    RUN_TEST(unary_csv_file_not_found_error);
    RUN_TEST(unary_csv_malformed_separators_error);
    // binary arithmetic
    RUN_TEST(binary_add_atom);
    RUN_TEST(binary_multiply_atom);
    RUN_TEST(binary_sub_atom);
    RUN_TEST(binary_sub_list_list);
    RUN_TEST(binary_sub_list_atom);
    RUN_TEST(binary_add_obj_list);
    RUN_TEST(binary_add_obj_obj);
    RUN_TEST(binary_add_obj_atom);
    RUN_TEST(binary_add_int_list_long);
    RUN_TEST(binary_add_int_atom_long);
    // comparison
    RUN_TEST(comparison_int_atom_true);
    RUN_TEST(comparison_int_atom_false);
    RUN_TEST(comparison_int_atom_list);
    RUN_TEST(comparison_int_list_list);
    RUN_TEST(comparison_tail_int_eql_list);
    RUN_TEST(comparison_tail_int_eql_atom);
    RUN_TEST(comparison_tail_char_eql_list);
    RUN_TEST(comparison_tail_char_eql_atom);
    RUN_TEST(comparison_tail_bool_and_list);
    RUN_TEST(comparison_tail_bool_and_atom);
    RUN_TEST(comparison_tail_bool_or_list);
    RUN_TEST(comparison_tail_bool_or_atom);
    RUN_TEST(comparison_tail_bool_eql_list);
    RUN_TEST(comparison_tail_bool_eql_atom);
    RUN_TEST(comparison_min_atom);
    RUN_TEST(comparison_min_atom_2);
    RUN_TEST(comparison_max_atom);
    RUN_TEST(comparison_max_atom_2);
    RUN_TEST(comparison_min_list);
    RUN_TEST(comparison_max_list);
    RUN_TEST(comparison_min_int_list_long);
    RUN_TEST(comparison_min_char_list_long);
    RUN_TEST(comparison_max_int_list_long);
    RUN_TEST(comparison_max_char_list_long);
    RUN_TEST(comparison_min_bool);
    RUN_TEST(comparison_max_bool);
    // squeeze
    RUN_TEST(squeeze_bool_eval);
    RUN_TEST(squeeze_bool_n1);
    RUN_TEST(squeeze_bool_n7);
    RUN_TEST(squeeze_bool_n8);
    RUN_TEST(squeeze_bool_n9);
    RUN_TEST(squeeze_bool_n64);
    RUN_TEST(squeeze_bool_n65);
    // assignment
    RUN_TEST(assignment_basic);
    RUN_TEST(assignment_basic_2);
    RUN_TEST(assignment_undefined_variable);
    RUN_TEST(assignment_undefined_in_expr);
    RUN_TEST(assignment_reassignment);
    RUN_TEST(assignment_op);
    // indexing
    RUN_TEST(index_str_with_atom);
    RUN_TEST(index_str_with_list);
    RUN_TEST(index_int_with_list);
    RUN_TEST(index_int_out_of_bounds);
    RUN_TEST(index_str_out_of_bounds);
    RUN_TEST(index_postfix_two_args);
    RUN_TEST(index_postfix_three_args);
    RUN_TEST(index_postfix_single_arg);
    RUN_TEST(apply_oob_multi_arg);
    RUN_TEST(apply_atom_rank_error);
    RUN_TEST(apply_cascade_rank_error);
    RUN_TEST(apply_string_cascade_rank_error);
    RUN_TEST(apply_too_many_args_rank_error);
    RUN_TEST(apply_chained_bracket_rank_error);
    RUN_TEST(apply_keyword_dyadic_rank);
    RUN_TEST(apply_nested_nary_stack);
    // take (#)
    RUN_TEST(binary_take_atom_int);
    RUN_TEST(binary_take_atom_char);
    RUN_TEST(binary_take_undertake);
    RUN_TEST(binary_take_exact);
    RUN_TEST(binary_take_overtake_cycle);
    RUN_TEST(binary_take_overtake_double);
    RUN_TEST(binary_take_overtake_boundary);
    RUN_TEST(binary_take_overtake_char);
    RUN_TEST(binary_take_undertake_squeeze);
    RUN_TEST(binary_take_undertake_stays_generic);
    RUN_TEST(binary_take_overtake_generic);
    RUN_TEST(binary_take_lambda_replicate);
    RUN_TEST(binary_take_negative_atom);
    RUN_TEST(binary_take_negative_atom_char);
    RUN_TEST(binary_take_negative_undertake);
    RUN_TEST(binary_take_negative_single);
    RUN_TEST(binary_take_negative_exact);
    RUN_TEST(binary_take_negative_overtake_clamps);
    RUN_TEST(binary_take_negative_char);
    RUN_TEST(binary_take_negative_char_overtake);
    RUN_TEST(binary_take_negative_generic);
    RUN_TEST(binary_drop_front);
    RUN_TEST(binary_drop_back);
    RUN_TEST(binary_drop_zero);
    RUN_TEST(binary_drop_exact);
    RUN_TEST(binary_drop_overdrop);
    RUN_TEST(binary_drop_overdrop_neg);
    RUN_TEST(binary_drop_char);
    RUN_TEST(binary_drop_char_back);
    RUN_TEST(binary_drop_generic);
    RUN_TEST(binary_drop_generic_back);
    RUN_TEST(binary_drop_squeeze);
    RUN_TEST(binary_drop_atom_y_type_error);
    RUN_TEST(binary_drop_nonint_x_type_error);
    RUN_TEST(binary_cut_char);
    RUN_TEST(binary_cut_squeeze_boxed);
    RUN_TEST(binary_cut_empty_segments);
    RUN_TEST(binary_cut_atom_x_type_error);
    RUN_TEST(binary_cut_atom_y_type_error);
    RUN_TEST(binary_cut_domain_error);
    RUN_TEST(binary_join_char_atoms);
    RUN_TEST(binary_join_char_list_atom);
    RUN_TEST(binary_join_char_atom_list);
    RUN_TEST(binary_join_int_atoms);
    RUN_TEST(binary_join_int_list_atom);
    RUN_TEST(binary_join_int_atom_list);
    RUN_TEST(binary_join_char_atom_obj);
    RUN_TEST(binary_join_obj_char_atom);
    RUN_TEST(binary_join_atom_lambda);
    RUN_TEST(binary_join_lambda_atom);
    RUN_TEST(binary_join_int_lists);
    RUN_TEST(binary_join_char_lists);
    RUN_TEST(binary_join_int_char_lists);
    RUN_TEST(binary_join_empty_diff_type);
    // lambdas
    RUN_TEST(lambda_eval_returns_lambda);
    RUN_TEST(lambda_eval_no_params);
    RUN_TEST(lambda_eval_single_param);
    RUN_TEST(lambda_eval_multi_params);
    RUN_TEST(lambda_apply_empty_body);
    RUN_TEST(lambda_apply_identity);
    RUN_TEST(lambda_apply_add);
    RUN_TEST(lambda_apply_multiply);
    RUN_TEST(lambda_apply_with_local);
    RUN_TEST(lambda_apply_multiple_locals);
    RUN_TEST(lambda_apply_local_and_param);
    RUN_TEST(lambda_apply_nested);
    RUN_TEST(lambda_apply_no_params);
    RUN_TEST(lambda_postfix_eval);
    RUN_TEST(lambda_reassign_param);
    RUN_TEST(lambda_error);
    RUN_TEST(lambda_error_undefined_var);
    RUN_TEST(lambda_set_get);
    RUN_TEST(lambda_rank_error);
    // parens / semicolons
    RUN_TEST(paren_eval_simple);
    RUN_TEST(paren_eval_grouping);
    RUN_TEST(paren_eval_nested);
    RUN_TEST(paren_eval_deep);
    RUN_TEST(paren_eval_multiple);
    RUN_TEST(semicolon_terminated_expr);
    RUN_TEST(expr_multiexpr_basic);
    RUN_TEST(expr_subexpr_with_ops);
    RUN_TEST(expr_subexpr_assignment);
    RUN_TEST(expr_fenced_subexpr_basic);
    RUN_TEST(expr_fenced_subexpr_with_ops);
    RUN_TEST(expr_fenced_subexpr_heterogeneous);
    // adverbs
    RUN_TEST(adverb_each_lambda_count);
    RUN_TEST(adverb_each_bare_op_count);
    RUN_TEST(adverb_each_atom_rank_error);
    RUN_TEST(adverb_bare_op_bracket_eval);

    RUN_TEST(adverb_each2_list_list);
    RUN_TEST(adverb_each2_atom_left);
    RUN_TEST(adverb_each2_atom_right);
    RUN_TEST(adverb_each2_atom_atom_rank_error);
    RUN_TEST(adverb_each2_length_error);
    // over1 (f/)
    RUN_TEST(adverb_over1_sum_fast);
    RUN_TEST(adverb_over1_mul_fast);
    RUN_TEST(adverb_over1_sub_fast);
    RUN_TEST(adverb_over1_max_generic);
    RUN_TEST(adverb_over1_min_generic);
    RUN_TEST(adverb_over1_nested);
    RUN_TEST(adverb_over1_empty_identity);
    RUN_TEST(adverb_over1_atom_rank_error);
    // scan1 (f\)
    RUN_TEST(adverb_scan1_sum);
    RUN_TEST(adverb_scan1_mul);
    RUN_TEST(adverb_scan1_sub);
    RUN_TEST(adverb_scan1_nested);
    // adverb stacking (each1 of over1/scan1)
    RUN_TEST(adverb_each1_over1);
    RUN_TEST(adverb_each1_scan1);
    // adverb error/cleanup paths
    RUN_TEST(adverb_over1_reduce_length_error);
    RUN_TEST(adverb_scan1_reduce_length_error);
    RUN_TEST(adverb_each1_inner_rank_error);
    // \t timing
    RUN_TEST(timeexpr_basic);
    RUN_TEST(timeexpr_iterations);
    RUN_TEST(timeexpr_error_no_space);
    RUN_TEST(timeexpr_error_bad_expr);

    printf("\n======================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests failed: %d\n", tests_failed);
    printf("Result:       %s\n", tests_failed ? "FAIL" : "PASS");
    printf("======================\n");
}

int main() {
    run_tests();
    return tests_failed ? 1 : 0;
}