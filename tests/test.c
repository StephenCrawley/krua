// krua test suite

#include "krua.h"
#include "eval.h"
#include "object.h"
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
    K r = eval(kcstr("&1=!3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "&1=!3 should return KIntType list");
    ASSERT(HDR_COUNT(r) == 1, "&1=!3 should have 1 element");
    ASSERT(INT_PTR(r)[0] == 1, "(&1=!3)[0] == 1");
    unref(r);
    PASS();
}

TEST(unary_where_multiple) {
    K r = eval(kcstr("&1=0 1 2 1 0 0"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "&1=0 1 2 1 0 0 should return KIntType list");
    ASSERT(HDR_COUNT(r) == 2, "&1=0 1 2 1 0 0 should have 2 elements");
    ASSERT(INT_PTR(r)[0] == 1, "(&1=0 1 2 1 0 0)[0] == 1");
    ASSERT(INT_PTR(r)[1] == 3, "(&1=0 1 2 1 0 0)[1] == 3");
    unref(r);
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
    K r = eval(kcstr("csv (1;\"iic\";\"tests/f.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType 2-tuple");
    ASSERT(HDR_COUNT(r) == 2, "tuple should have 2 elements (syms; cols)");
    K syms = OBJ_PTR(r)[0];
    ASSERT(!IS_TAG(syms) && HDR_TYPE(syms) == KSymType && HDR_COUNT(syms) == 3, "syms should be 3 KSymType");
    K cols = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(cols) && HDR_TYPE(cols) == KObjType && HDR_COUNT(cols) == 3, "cols should be 3 KObjType");
    ASSERT_2_INTS(OBJ_PTR(cols)[0], 1, 7);
    ASSERT_2_INTS(OBJ_PTR(cols)[1], 2, 11);
    K c2 = OBJ_PTR(cols)[2];
    ASSERT(!IS_TAG(c2) && HDR_TYPE(c2) == KChrType && HDR_COUNT(c2) == 2, "col 2 should be 2 KChrType");
    ASSERT(CHR_PTR(c2)[0] == 'a' && CHR_PTR(c2)[1] == 'b', "col 2 bytes should be 'a','b'");
    unref(r);
    PASS();
}

TEST(unary_csv_header_skip_middle) {
    K r = eval(kcstr("csv (1;\"i c\";\"tests/f.csv\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "should return KObjType 2-tuple");
    ASSERT(HDR_COUNT(r) == 2, "tuple should have 2 elements (syms; cols)");
    K cols = OBJ_PTR(r)[1];
    ASSERT(!IS_TAG(cols) && HDR_TYPE(cols) == KObjType && HDR_COUNT(cols) == 2, "cols should be 2 KObjType");
    ASSERT_2_INTS(OBJ_PTR(cols)[0], 1, 7);
    K c1 = OBJ_PTR(cols)[1];
    ASSERT(!IS_TAG(c1) && HDR_TYPE(c1) == KChrType && HDR_COUNT(c1) == 2, "col 1 should be 2 KChrType");
    ASSERT(CHR_PTR(c1)[0] == 'a' && CHR_PTR(c1)[1] == 'b', "col 1 bytes should be 'a','b'");
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
    K r = eval(kcstr("1+2"));
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 3, "1+2 should evaluate to 3");
    PASS();
}

TEST(binary_multiply_atom) {
    K r = eval(kcstr("3*4"));
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 12, "3*4 should evaluate to 12");
    PASS();
}

TEST(binary_sub_atom) {
    K r = eval(kcstr("1-2"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType, "1-2 should return int atom");
    ASSERT(TAG_VAL(r) == -1, "1-2 should be -1");
    PASS();
}

TEST(binary_sub_list_list) { // TODO: BINARY_OP doesn't handle list-list yet
    K r = eval(kcstr("1 2-3 4"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "1 2-3 4 should return int list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    ASSERT(INT_PTR(r)[0] == -2 && INT_PTR(r)[1] == -2, "1 2-3 4 should be -2 -2");
    unref(r);
    PASS();
}

TEST(binary_sub_list_atom) {
    K r = eval(kcstr("1 2 3-1"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "1 2 3-1 should return int list");
    ASSERT(HDR_COUNT(r) == 3, "result should have 3 elements");
    ASSERT(INT_PTR(r)[0] == 0 && INT_PTR(r)[1] == 1 && INT_PTR(r)[2] == 2, "1 2 3-1 should be 0 1 2");
    unref(r);
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

TEST(comparison_tail_ok) {
    // last word gets zeroed after compare... make sure trailing bools are ok
    K r = eval(kcstr("(!10)=!10"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KBoolType, "(!10)=!10 should return KBoolType list");
    ASSERT(CHR_PTR(r)[8] == 1, "((!10)=!10)[8] == 1");
    ASSERT(CHR_PTR(r)[9] == 1, "((!10)=!10)[9] == 1");
    unref(r);
    PASS();
}

TEST(comparison_min_atom) {
    K r = eval(kcstr("1&2"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 1, "should return minimum value 1");
    PASS();
}

TEST(comparison_min_atom_2) {
    K r = eval(kcstr("2&1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 1, "should return minimum value 1");
    PASS();
}

TEST(comparison_max_atom) {
    K r = eval(kcstr("1|2"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 2, "should return maximum value 2");
    PASS();
}

TEST(comparison_max_atom_2) {
    K r = eval(kcstr("2|1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 2, "should return maximum value 2");
    PASS();
}

TEST(comparison_min_list) {
    K r = eval(kcstr("1 & 1 0 2 3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType && HDR_COUNT(r) == 4, "should return 4-element int list");
    ASSERT(INT_PTR(r)[0] == 1, "element 0 should be 1");
    ASSERT(INT_PTR(r)[1] == 0, "element 1 should be 0");
    ASSERT(INT_PTR(r)[2] == 1, "element 2 should be 1");
    ASSERT(INT_PTR(r)[3] == 1, "element 3 should be 1");
    unref(r);
    PASS();
}

TEST(comparison_max_list) {
    K r = eval(kcstr("1 | 1 0 2 3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType && HDR_COUNT(r) == 4, "should return 4-element int list");
    ASSERT(INT_PTR(r)[0] == 1, "element 0 should be 1");
    ASSERT(INT_PTR(r)[1] == 1, "element 1 should be 1");
    ASSERT(INT_PTR(r)[2] == 2, "element 2 should be 2");
    ASSERT(INT_PTR(r)[3] == 3, "element 3 should be 3");
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
    ASSERT(CHR_PTR(r)[0] == 1, "element 0 should be 1");
    ASSERT(CHR_PTR(r)[1] == 1, "element 1 should be 1");
    ASSERT(CHR_PTR(r)[2] == 1, "element 2 should be 1");
    unref(r);
    PASS();
}

// Runtime: assignment
TEST(assignment_basic) {
    K r = eval(kcstr("x:42"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym("x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 42, "x should be assigned 42");
    PASS();
}

TEST(assignment_basic_2) {
    K r = eval(kcstr("x:42+1"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym("x", 1));
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
    K r = eval(kcstr("f:+; f[1;6]"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 7, "operator assignment works");
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
    K r = eval(kcstr("3 2 1@0"));
    ASSERT(r && IS_TAG(r), "indexing at 0 should return atom");
    ASSERT(TAG_TYPE(r) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(r) == 3, "first element should be 3");
    PASS();
}

TEST(index_int_out_of_bounds){
    K r = eval(kcstr("1 2@3"));
    ASSERT(r && IS_TAG(r), "out of bounds index should return atom");
    ASSERT(TAG_TYPE(r) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(r) == 0, "out of bounds int index should return 0");
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
    K r = eval(kcstr("(1 2;3 4)[1;0]"));
    ASSERT(r && IS_TAG(r), "result should be an atom");
    ASSERT(TAG_TYPE(r) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(r) == 3, "result should be 3");
    PASS();
}

TEST(index_postfix_three_args){
    K r = eval(kcstr("((1 2;3 4);(5 6;7 8))[1;0;1]"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 6, "should be 6");
    PASS();
}

TEST(index_postfix_single_arg){
    K r = eval(kcstr("1 2 3[0]"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 1, "should be 1");
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
    K r = eval(kcstr("{[x]x}@42"));
    ASSERT(r && TAG_VAL(r) == 42, "{[x]x}@42 should be 42");
    PASS();
}

TEST(lambda_apply_add) {
    K r = eval(kcstr("{[x]x+1}@2"));
    ASSERT(r && TAG_VAL(r) == 3, "{[x]x+1}@2 should be 3");
    PASS();
}

TEST(lambda_apply_multiply) {
    K r = eval(kcstr("{[x]x*2}@10"));
    ASSERT(r && TAG_VAL(r) == 20, "{[x]x*2}@10 should be 20");
    PASS();
}

TEST(lambda_apply_with_local) {
    K r = eval(kcstr("{[x]y:x+1}@6"));
    ASSERT(r && TAG_VAL(r) == 7, "lambda with local: {[x]y:x+1;y}@5 should be 6");
    PASS();
}

TEST(lambda_apply_multiple_locals) {
    K r = eval(kcstr("{[x]z:y:x+1}@6"));
    ASSERT(r && TAG_VAL(r) == 7, "multiple locals: (5+1)*2=12");
    PASS();
}

TEST(lambda_apply_local_and_param) {
    K r = eval(kcstr("{[x]x+y:x+1}@5"));
    ASSERT(r && TAG_VAL(r) == 11, "local+param: (5+1)+5=11");
    PASS();
}

TEST(lambda_apply_nested) {
    K r = eval(kcstr("{[x]x+{[y]y*2}@3}@5"));
    ASSERT(r && TAG_VAL(r) == 11, "nested lambda: 5+(3*2)=11");
    PASS();
}

TEST(lambda_apply_no_params) {
    K r = eval(kcstr("{[]42}"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "{[]42} should eval to lambda");
    unref(r);
    PASS();
}

TEST(lambda_postfix_eval){
    K r = eval(kcstr("{[x]x+1}[6]"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 7, "{[x]x+1}[6] should be 7");
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
    K r = eval(kcstr("{[x]a:x+1; a+2} 4"));
    ASSERT(r, "should evaluate without error");
    ASSERT(IS_TAG(r) && TAG_TYPE(r) == KIntType && TAG_VAL(r) == 7, "should return int-atom with value 7");
    PASS();
}

// Runtime: parens / semicolons
TEST(paren_eval_simple) {
    K r = eval(kcstr("(42)"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 42, "(42) should be 42");
    PASS();
}

TEST(paren_eval_grouping) {
    K r = eval(kcstr("(1+2)*3"));
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 9, "(1+2)*3 should be 9");
    PASS();
}

TEST(paren_eval_nested) {
    K r = eval(kcstr("((1+2))"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "((1+2)) should be 3");
    PASS();
}

TEST(paren_eval_deep) {
    K r = eval(kcstr("(((1+2)))"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "(((1+2))) should be 3");
    PASS();
}

TEST(paren_eval_multiple) {
    K r = eval(kcstr("(1+2)+(3+4)"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 10, "(1+2)+(3+4) should be 10");
    PASS();
}

TEST(semicolon_terminated_expr) {
    K r = eval(kcstr("1 2;"));
    ASSERT(r == knull(), "semicolon-terminated expression should return knull");
    PASS();
}

TEST(expr_multiexpr_basic) {
    K r = eval(kcstr("1 2;2"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 2, "should return 2");
    PASS();
}

TEST(expr_subexpr_with_ops) {
    K r = eval(kcstr("1+2;3*4"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 12, "1+2;3*4 should return 12");
    PASS();
}

TEST(expr_subexpr_assignment) {
    K r = eval(kcstr("x:1;x+2"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "x:1;x+2 should return 3");
    PASS();
}

TEST(expr_fenced_subexpr_basic) {
    K r = eval(kcstr("(1;2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 2, "(1;2) should be 1 2");
    unref(r);
    PASS();
}

TEST(expr_fenced_subexpr_with_ops) {
    K r = eval(kcstr("(1;2+3)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2+3) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 5, "(1;2+3) should be 1 5");
    unref(r);
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
    K r = eval(kcstr("{[x]#x}'(1 2;3 4 5)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "should return int list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    ASSERT(INT_PTR(r)[0] == 2 && INT_PTR(r)[1] == 3, "{[x]#x}'(1 2;3 4 5) should be 2 3");
    unref(r);
    PASS();
}

TEST(adverb_each_bare_op_count) {
    K r = eval(kcstr("#'(1 2;3 4 5)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "should return int list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    ASSERT(INT_PTR(r)[0] == 2 && INT_PTR(r)[1] == 3, "#'(1 2;3 4 5) should be 2 3");
    unref(r);
    PASS();
}

TEST(adverb_each_atom_rank_error) {
    K r = eval(kcstr("#'1"));
    ASSERT(!r && kerrno == KERR_RANK, "each on atom should be rank error");
    PASS();
}

TEST(adverb_bare_op_bracket_eval) {
    K r = eval(kcstr("+[1;2]"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "+[1;2] should be 3");
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
    // variables
    RUN_TEST(tokenize_single_variable);
    RUN_TEST(tokenize_multiple_variables);
    // operators
    RUN_TEST(tokenize_binary_add);
    RUN_TEST(tokenize_unary_plus);
    RUN_TEST(tokenize_assignment);
    RUN_TEST(tokenize_csv_keyword);
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
    RUN_TEST(unary_til);
    RUN_TEST(unary_keyword_til);
    RUN_TEST(unary_count_list);
    RUN_TEST(unary_count_atom);
    RUN_TEST(unary_keyword_count_list);
    RUN_TEST(unary_keyword_count_atom);
    RUN_TEST(unary_value_basic);
    RUN_TEST(unary_value_file_not_found);
    RUN_TEST(unary_value_type_error);
    RUN_TEST(unary_where_single);
    RUN_TEST(unary_where_multiple);
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
    // comparison
    RUN_TEST(comparison_int_atom_true);
    RUN_TEST(comparison_int_atom_false);
    RUN_TEST(comparison_int_atom_list);
    RUN_TEST(comparison_int_list_list);
    RUN_TEST(comparison_tail_ok);
    RUN_TEST(comparison_min_atom);
    RUN_TEST(comparison_min_atom_2);
    RUN_TEST(comparison_max_atom);
    RUN_TEST(comparison_max_atom_2);
    RUN_TEST(comparison_min_list);
    RUN_TEST(comparison_max_list);
    RUN_TEST(comparison_min_bool);
    RUN_TEST(comparison_max_bool);
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