// krua test suite

#include "krua.h"
#include "eval.h"
#include "object.h"

#ifdef TRACK_REFS
#include "refcount.h"
#endif

static int tests_run = 0;
static int tests_failed = 0;

#ifdef TRACK_REFS
// With tracking: automatic GLOBALS creation and leak checking
#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  %-30s", #name); \
    reset(); \
    GLOBALS = ksymdict(); \
    test_##name(); \
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
    printf("  %-30s", #name); \
    GLOBALS = ksymdict(); \
    test_##name(); \
    unref(GLOBALS); \
    tests_run++; \
} while(0)

#define PASS() printf("PASS\n")

#endif

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); tests_failed++; return; } } while(0)

/* Testing Practices:
 * - Use kcstr(s) to create KChrType from null-terminated C strings
 * - Use IS_CLASS(class, byte) macro for bytecode range checks, not manual arithmetic
 * - Keep tests simple and direct - no frameworks, just ASSERT and PASS
 * - Error tests will print to stdout (messy output) - this is expected and proves errors are caught
 * - Each test should clean up heap allocations with unref() (tags don't need unref)
 * - RUN_TEST macro handles GLOBALS creation automatically
 * - Use tokenize() helper for tests that only need the token stream
 *   Call token() directly only when you need access to vars or consts
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

// Strip/sanitization tests
TEST(strip_leading_comment) {
    const char *src = "/ comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 0, "leading '/' should empty the string");
    unref(x);
    PASS();
}

TEST(strip_trailing_comment) {
    const char *src = "1+2 / comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to '1+2'");
    ASSERT(memcmp(CHR_PTR(x), "1+2", 3) == 0, "content should be '1+2'");
    unref(x);
    PASS();
}

TEST(strip_trailing_whitespace) {
    const char *src = "abc   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip whitespace");
    ASSERT(memcmp(CHR_PTR(x), "abc", 3) == 0, "content should be 'abc'");
    unref(x);
    PASS();
}

TEST(strip_both) {
    const char *src = "x:1 / assign   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to 'x:1'");
    unref(x);
    PASS();
}

TEST(only_whitespace) {
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

// Basic tokenization tests
TEST(empty_input) {
    K r = tokenize("");
    ASSERT(r && HDR_COUNT(r) == 0, "empty should produce 0 tokens");
    unref(r);
    PASS();
}

TEST(invalid_token) {
    K r = tokenize("£");
    ASSERT(!r, "'£' should fail as invalid token");
    ASSERT(kerrno == KERR_PARSE, "invalid token should raise parse error");
    PASS();
}

TEST(single_variable) {
    K r = tokenize("abc");
    ASSERT(r && HDR_COUNT(r) == 1, "single var should produce 1 token");
    ASSERT(IS_CLASS(OP_GET_VAR, CHR_PTR(r)[0]), "variable should be in GET_VAR range");
    unref(r);
    PASS();
}

TEST(single_integer) {
    K r = tokenize("123");
    ASSERT(r && HDR_COUNT(r) == 1, "single int should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "int should be in CONST range");
    unref(r);
    PASS();
}

TEST(integer_list) {
    K r = tokenize("123 456 789");
    ASSERT(r && HDR_COUNT(r) == 1, "int list should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "list should be in CONST range");
    unref(r);
    PASS();
}

TEST(string_literal) {
    K r = tokenize("\"hello\"");
    ASSERT(r && HDR_COUNT(r) == 1, "string should produce 1 token");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[0]), "string should be in CONST range");
    unref(r);
    PASS();
}

TEST(char_literal) {
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

TEST(trailing_whitespace) {
    K r = tokenize("123   ");
    ASSERT(r && HDR_COUNT(r) == 1, "should ignore trailing whitespace");
    unref(r);
    PASS();
}

TEST(lambda_simple) {
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

TEST(lambda_stores_full_src) {
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

TEST(lambda_nested) {
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

TEST(empty_parens_token) {
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

// Expression tokenization tests
TEST(binary_add) {
    K r = tokenize("123+456");
    ASSERT(r && HDR_COUNT(r) == 3, "binary op should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 1, "+ should be operator 1 (OPS[:+-*...])");
    unref(r);
    PASS();
}

TEST(unary_plus) {
    K r = tokenize("+123");
    ASSERT(r && HDR_COUNT(r) == 2, "unary op should produce 2 tokens");
    ASSERT(CHR_PTR(r)[0] == 1, "+ should be operator 1");
    unref(r);
    PASS();
}

TEST(assignment) {
    K r = tokenize("a:42");
    ASSERT(r && HDR_COUNT(r) == 3, "assignment should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 0, ": should be operator 0");
    unref(r);
    PASS();
}

TEST(multiple_variables) {
    K r = tokenize("x y z");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 variable tokens");
    unref(r);
    PASS();
}

TEST(paren_token_passthrough) {
    K r = tokenize("(1)");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 tokens");
    ASSERT(CHR_PTR(r)[0] == '(', "first should be literal (");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(r)[1]), "second should be CONST");
    ASSERT(CHR_PTR(r)[2] == ')', "third should be literal )");
    unref(r);
    PASS();
}

// Compilation tests
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
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 5), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

TEST(compile_application2) {
    K tokens = tokenize("\"abc\" 0");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "application should compile");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should PUSH const");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 5), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

TEST(lambda_postfix_single_arg) {
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

TEST(lambda_postfix_two_args) {
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

TEST(paren_compile_simple) {
    K tokens = tokenize("(1)");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "should compile to 1 byte");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(paren_compile_nested) {
    K tokens = tokenize("((1))");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "nested parens should compile to 1 byte");
    ASSERT(IS_CLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(paren_compile_with_op) {
    K tokens = tokenize("(1+2)*3");
    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 5, "should compile to 5 bytes");
    ASSERT(CHR_PTR(bytecode)[4] == OP_BINARY + 3, "last should be BINARY *");
    unref(bytecode);
    PASS();
}

// VM execution tests
TEST(vm_simple_add) {
    K r = eval(kcstr("1+2"));
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 3, "1+2 should evaluate to 3");
    PASS();
}

TEST(vm_simple_multiply) {
    K r = eval(kcstr("3*4"));
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 12, "3*4 should evaluate to 12");
    PASS();
}

TEST(vm_sub_atom) {
    K r = eval(kcstr("1-2"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType, "1-2 should return int atom");
    ASSERT(TAG_VAL(r) == -1, "1-2 should be -1");
    PASS();
}

TEST(vm_sub_list_list) { // TODO: BINARY_OP doesn't handle list-list yet
    K r = eval(kcstr("1 2-3 4"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "1 2-3 4 should return int list");
    ASSERT(HDR_COUNT(r) == 2, "result should have 2 elements");
    ASSERT(INT_PTR(r)[0] == -2 && INT_PTR(r)[1] == -2, "1 2-3 4 should be -2 -2");
    unref(r);
    PASS();
}

TEST(vm_sub_list_atom) {
    K r = eval(kcstr("1 2 3-1"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "1 2 3-1 should return int list");
    ASSERT(HDR_COUNT(r) == 3, "result should have 3 elements");
    ASSERT(INT_PTR(r)[0] == 0 && INT_PTR(r)[1] == 1 && INT_PTR(r)[2] == 2, "1 2 3-1 should be 0 1 2");
    unref(r);
    PASS();
}

TEST(add_lists_1) {
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

TEST(add_lists_2) {
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

TEST(add_lists_obj_atom) {
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

TEST(vm_neg_atom) {
    K r = eval(kcstr("- 1"));
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KIntType, "- 1 should return int atom");
    ASSERT(TAG_VAL(r) == -1, "- 1 should be -1");
    PASS();
}

TEST(vm_neg_list) {
    K r = eval(kcstr("- 1 2 3"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "- 1 2 3 should return int list");
    ASSERT(HDR_COUNT(r) == 3, "result should have 3 elements");
    ASSERT(INT_PTR(r)[0] == -1 && INT_PTR(r)[1] == -2 && INT_PTR(r)[2] == -3, "- 1 2 3 should be -1 -2 -3");
    unref(r);
    PASS();
}

TEST(vm_assignment) {
    K r = eval(kcstr("x:42"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym("x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 42, "x should be assigned 42");
    PASS();
}

TEST(vm_assignment_2) {
    K r = eval(kcstr("x:42+1"));
    ASSERT(r == knull(), "assignment as final op should return knull");
    K x_val = getGlobal(encodeSym("x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 43, "x should be assigned 43");
    PASS();
}

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

TEST(lambda_postfix_eval){
    K r = eval(kcstr("{[x]x+1}[6]"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 7, "{[x]x+1}[6] should be 7");
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

// Lambda application tests
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

TEST(lambda_reassign_param) {
    K r = eval(kcstr("{[x]x:\"new\"}@\"old\""));
    // If double-free, likely crashes or fails leak test
    unref(r);
    PASS();
}

TEST(lambda_error) {
    // runtime error inside lambda is handled correctly
    K r = eval(kcstr("{[x]. x}@1"));
    ASSERT(!r, "applying lambda to string should fail");
    ASSERT(kerrno == KERR_TYPE, "should raise type error");
    PASS();
}

TEST(lambda_error_undefined_var) {
    K r = eval(kcstr("{[x]x+z}@5"));
    ASSERT(!r, "referencing undefined variable in lambda body should return error");
    ASSERT(kerrno == KERR_VALUE, "should raise value error");
    PASS();
}

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

TEST(multiexpr_basic) {
    K r = eval(kcstr("1 2;2"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 2, "should return 2");
    PASS();
}

TEST(subexpr_with_ops) {
    K r = eval(kcstr("1+2;3*4"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 12, "1+2;3*4 should return 12");
    PASS();
}

TEST(subexpr_assignment) {
    K r = eval(kcstr("x:1;x+2"));
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "x:1;x+2 should return 3");
    PASS();
}

TEST(fenced_subexpr_basic) {
    K r = eval(kcstr("(1;2)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 2, "(1;2) should be 1 2");
    unref(r);
    PASS();
}

TEST(fenced_subexpr_with_ops) {
    K r = eval(kcstr("(1;2+3)"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2+3) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 5, "(1;2+3) should be 1 5");
    unref(r);
    PASS();
}

TEST(fenced_subexpr_heterogeneous) {
    K r = eval(kcstr("(1;\"a\")"));
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1;\"a\") should return generic list");
    ASSERT(HDR_COUNT(r) == 2, "(1;\"a\") should have 2 elements");
    ASSERT(TAG_VAL(OBJ_PTR(r)[0]) == 1, "first element should be 1");
    ASSERT(TAG_VAL(OBJ_PTR(r)[1]) == 'a', "second element should be 'a'");
    unref(r);
    PASS();
}

// Error tests
TEST(unclosed_string) {
    K r = tokenize("\"hello");
    ASSERT(!r, "unclosed string should fail");
    ASSERT(kerrno == KERR_PARSE, "unclosed string should raise parse error");
    PASS();
}

TEST(parse_unclosed_string_in_expr) {
    K r = tokenize("1+2+\"hello");
    ASSERT(!r, "unclosed string in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(parse_invalid_token_in_expr) {
    K r = tokenize("1+£+2");
    ASSERT(!r, "invalid token in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(parse_single_quote) {
    K r = tokenize("\"");
    ASSERT(!r, "single quote should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(empty_string_literal) {
    K r = eval(kcstr("\"\""));
    ASSERT(r != 0, "empty string should be valid");
    ASSERT(HDR_TYPE(r) == KChrType, "should be KChrType");
    ASSERT(HDR_COUNT(r) == 0, "should have length 0");
    unref(r);
    PASS();
}

TEST(value_undefined_in_expr) {
    K r = eval(kcstr("1+foo+2"));
    ASSERT(!r, "undefined variable in expression should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise KERR_VALUE");
    PASS();
}

TEST(vm_undefined_variable) {
    K r = eval(kcstr("foo"));
    ASSERT(!r, "undefined variable should return error");
    ASSERT(kerrno == KERR_VALUE, "undefined variable should raise value error");
    PASS();
}

TEST(lambda_error_missing_bracket) {
    K r = eval(kcstr("{x}"));
    ASSERT(!r, "should error: lambda must have params");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(lambda_error_unclosed_params) {
    K r = eval(kcstr("{[x"));
    ASSERT(!r, "should error: unclosed param list");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(lambda_error_unclosed_lambda) {
    K r = eval(kcstr("{[x]x+1"));
    ASSERT(!r, "should error: unclosed lambda");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(error_unmatched_paren) {
    K r = eval(kcstr("(1+2"));
    ASSERT(!r, "unmatched paren should error");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

// Monad tests
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

// Adverb compilation tests
TEST(adverb_each_infix) {
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
    ASSERT(bc[3] == OP_VERB + 20, "each wrap");
    ASSERT(bc[4] == OP_N_ARY + 2, "apply 2");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(adverb_each_postfix_bracket) {
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
    ASSERT(bc[2] == OP_VERB + 20, "each wrap");
    ASSERT(bc[3] == OP_N_ARY + 1, "apply 1");
    ASSERT(IS_CLASS(OP_GET_VAR, bc[4]), "load x");
    ASSERT(bc[5] == OP_BINARY + 5, "binary apply");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(adverb_bare_op_unary) {
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
    ASSERT(bc[2] == OP_VERB + 20, "each wrap");
    ASSERT(bc[3] == OP_N_ARY + 1, "apply 1");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(adverb_bare_op_infix) {
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
    ASSERT(bc[3] == OP_VERB + 20, "each wrap");
    ASSERT(bc[4] == OP_N_ARY + 2, "apply 2");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(adverb_bare_no_args) {
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
    ASSERT(bc[1] == OP_VERB + 20, "each wrap");
    ASSERT(IS_CLASS(OP_SET_VAR, bc[2]), "set g");
    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

// Test runner
void run_tests() {
    printf("\nPreprocessing:\n");
    RUN_TEST(strip_leading_comment);
    RUN_TEST(strip_trailing_comment);
    RUN_TEST(strip_trailing_whitespace);
    RUN_TEST(strip_both);
    RUN_TEST(only_whitespace);
    //RUN_TEST(ignore_quoted_slash);

    printf("\nTokenization:\n");
    RUN_TEST(empty_input);
    RUN_TEST(single_variable);
    RUN_TEST(multiple_variables);
    RUN_TEST(single_integer);
    RUN_TEST(integer_list);
    RUN_TEST(string_literal);
    RUN_TEST(char_literal);
    RUN_TEST(trailing_whitespace);
    RUN_TEST(lambda_simple);
    RUN_TEST(lambda_stores_full_src);
    RUN_TEST(lambda_nested);
    RUN_TEST(binary_add);
    RUN_TEST(unary_plus);
    RUN_TEST(assignment);
    RUN_TEST(paren_token_passthrough);
    RUN_TEST(empty_parens_token);

    printf("\nCompilation:\n");
    RUN_TEST(compile_empty);
    RUN_TEST(compile_constant);
    RUN_TEST(compile_binary_op);
    RUN_TEST(compile_unary_op);
    RUN_TEST(compile_assignment);
    RUN_TEST(compile_application);
    RUN_TEST(compile_application2);
    RUN_TEST(lambda_postfix_single_arg);
    RUN_TEST(lambda_postfix_two_args);
    RUN_TEST(paren_compile_simple);
    RUN_TEST(paren_compile_nested);
    RUN_TEST(paren_compile_with_op);

    printf("\nVM Execution:\n");
    RUN_TEST(vm_simple_add);
    RUN_TEST(vm_simple_multiply);
    RUN_TEST(vm_sub_atom);
    RUN_TEST(vm_sub_list_list);
    RUN_TEST(vm_sub_list_atom);
    RUN_TEST(add_lists_1);
    RUN_TEST(add_lists_2);
    RUN_TEST(add_lists_obj_atom);
    RUN_TEST(vm_neg_atom);
    RUN_TEST(vm_neg_list);
    RUN_TEST(vm_assignment);
    RUN_TEST(vm_assignment_2);
    RUN_TEST(empty_string_literal);
    RUN_TEST(index_str_with_atom);
    RUN_TEST(index_str_with_list);
    RUN_TEST(index_int_with_list);
    RUN_TEST(index_int_out_of_bounds);
    RUN_TEST(index_str_out_of_bounds);
    RUN_TEST(index_postfix_two_args);
    RUN_TEST(index_postfix_three_args);
    RUN_TEST(index_postfix_single_arg);
    RUN_TEST(lambda_postfix_eval);
    RUN_TEST(apply_oob_multi_arg);
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
    RUN_TEST(lambda_reassign_param);
    RUN_TEST(lambda_error);
    RUN_TEST(lambda_error_undefined_var);
    RUN_TEST(paren_eval_simple);
    RUN_TEST(paren_eval_grouping);
    RUN_TEST(paren_eval_nested);
    RUN_TEST(paren_eval_deep);
    RUN_TEST(paren_eval_multiple);
    RUN_TEST(semicolon_terminated_expr);
    RUN_TEST(multiexpr_basic);
    RUN_TEST(subexpr_with_ops);
    RUN_TEST(subexpr_assignment);
    RUN_TEST(fenced_subexpr_basic);
    RUN_TEST(fenced_subexpr_with_ops);
    RUN_TEST(fenced_subexpr_heterogeneous);

    printf("\nError Handling:\n");
    RUN_TEST(invalid_token);
    RUN_TEST(unclosed_string);
    RUN_TEST(parse_unclosed_string_in_expr);
    RUN_TEST(parse_invalid_token_in_expr);
    RUN_TEST(parse_single_quote);
    RUN_TEST(vm_undefined_variable);
    RUN_TEST(value_undefined_in_expr);
    RUN_TEST(lambda_error_missing_bracket);
    RUN_TEST(lambda_error_unclosed_params);
    RUN_TEST(lambda_error_unclosed_lambda);
    RUN_TEST(error_unmatched_paren);
    RUN_TEST(apply_atom_rank_error);
    RUN_TEST(apply_cascade_rank_error);
    RUN_TEST(apply_string_cascade_rank_error);
    RUN_TEST(apply_too_many_args_rank_error);
    RUN_TEST(apply_chained_bracket_rank_error);

    printf("\nAdverbs:\n");
    RUN_TEST(adverb_each_infix);
    RUN_TEST(adverb_each_postfix_bracket);
    RUN_TEST(adverb_bare_op_unary);
    RUN_TEST(adverb_bare_op_infix);
    RUN_TEST(adverb_bare_no_args);

    printf("\nMonads:\n");
    RUN_TEST(unary_value_basic);
    RUN_TEST(unary_value_file_not_found);
    RUN_TEST(unary_value_type_error);

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