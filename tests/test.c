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
#define TEST(name) static void test_##name(K GLOBALS)

#define RUN_TEST(name) do { \
    printf("  %-30s", #name); \
    reset(); \
    K GLOBALS = ksymdict(); \
    test_##name(GLOBALS); \
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
#define TEST(name) static void test_##name(K GLOBALS)

#define RUN_TEST(name) do { \
    printf("  %-30s", #name); \
    K GLOBALS = ksymdict(); \
    test_##name(GLOBALS); \
    unref(GLOBALS); \
    tests_run++; \
} while(0)

#define PASS() printf("PASS\n")

#endif

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); tests_failed++; return; } } while(0)

/* Testing Practices:
 * - Always use `const char *src = "..."` pattern for string literals, never inline them
 * - Use strlen(src) for char* lengths if needed. Never hardcode magic numbers like 3, 4, etc, for source lengths
 * - Use kcstr(s) to create KChrType from null-terminated C strings
 * - Use ISCLASS(class, byte) macro for bytecode range checks, not manual arithmetic
 * - Keep tests simple and direct - no frameworks, just ASSERT and PASS
 * - Error tests will print to stdout (messy output) - this is expected and proves errors are caught
 * - Each test should clean up its allocations with unref()
 * - GLOBALS is now passed as parameter - use it for eval() calls
 * - This allows both tracked and untracked builds to work from same source
 * - Tests that don't use GLOBALS should have (void)GLOBALS; to suppress warnings
 * - RUN_TEST macro handles GLOBALS creation automatically
 * 
 * - Test are logically grouped in the test runner to test:
 *   - preprocessing
 *   - tokenization
 *   - compilation
 *   - vm
 *   - error handling
 *   - edge cases
 */

// Test helpers
static K tokenize(const char *src) {
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K result = token(x, &vars, &consts);
    unref(x), unref(vars); unref(consts);
    return result;
}

static K compile_tokens(K tokens) {
    if (!tokens) return 0;
    K r = compile(0, tokens, 0);
    return r;
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
    (void)GLOBALS;  // Unused in this test
    const char *src = "/ comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 0, "leading '/' should empty the string");
    unref(x);
    PASS();
}

TEST(strip_trailing_comment) {
    (void)GLOBALS;
    const char *src = "1+2 / comment";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to '1+2'");
    ASSERT(memcmp(CHR_PTR(x), "1+2", 3) == 0, "content should be '1+2'");
    unref(x);
    PASS();
}

TEST(strip_trailing_whitespace) {
    (void)GLOBALS;
    const char *src = "abc   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip whitespace");
    ASSERT(memcmp(CHR_PTR(x), "abc", 3) == 0, "content should be 'abc'");
    unref(x);
    PASS();
}

TEST(strip_both) {
    (void)GLOBALS;
    const char *src = "x:1 / assign   ";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == 3, "should strip to 'x:1'");
    unref(x);
    PASS();
}

TEST(only_whitespace) {
    (void)GLOBALS;
    const char *src = "   ";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result != 0, "whitespace-only string should be valid");
    ASSERT(result == knull(), "whitespace-only string should return K generic null");
    PASS();
}

/*TEST(ignore_quoted_slash){
    (void)GLOBALS;
    const char *src = "\"ignore / in quotes\"";
    K x = kcstr(src);
    strip(x);
    ASSERT(HDR_COUNT(x) == strlen(src), "should not mistake quoted forward-slash for comment");
    unref(x);
    PASS();
}*/

// Basic tokenization tests
TEST(empty_input) {
    (void)GLOBALS;
    K r = tokenize("");
    ASSERT(r && HDR_COUNT(r) == 0, "empty should produce 0 tokens");
    unref(r);
    PASS();
}

TEST(invalid_token) {
    (void)GLOBALS;
    // just test a token we don't handle
    K vars = 0, consts = 0;
    const char *src = "£";
    K x = kcstr(src);
    K r = token(x, &vars, &consts);
    ASSERT(r == 0, "'£' should fail as invalid token");
    ASSERT(kerrno == KERR_PARSE, "invalid token should raise parse error");
    unref(x), unref(vars); unref(consts);
    PASS();
}

TEST(single_variable) {
    (void)GLOBALS;
    K r = tokenize("abc");
    ASSERT(r && HDR_COUNT(r) == 1, "single var should produce 1 token");
    ASSERT(ISCLASS(OP_GET_VAR, CHR_PTR(r)[0]), "variable should be in GET_VAR range");
    unref(r);
    PASS();
}

TEST(single_integer) {
    (void)GLOBALS;
    K r = tokenize("123");
    ASSERT(r && HDR_COUNT(r) == 1, "single int should produce 1 token");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(r)[0]), "int should be in CONST range");
    unref(r);
    PASS();
}

TEST(integer_list) {
    (void)GLOBALS;
    K r = tokenize("123 456 789");
    ASSERT(r && HDR_COUNT(r) == 1, "int list should produce 1 token");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(r)[0]), "list should be in CONST range");
    unref(r);
    PASS();
}

TEST(string_literal) {
    (void)GLOBALS;
    K r = tokenize("\"hello\"");
    ASSERT(r && HDR_COUNT(r) == 1, "string should produce 1 token");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(r)[0]), "string should be in CONST range");
    unref(r);
    PASS();
}

TEST(trailing_whitespace) {
    (void)GLOBALS;
    K r = tokenize("123   ");
    ASSERT(r && HDR_COUNT(r) == 1, "should ignore trailing whitespace");
    unref(r);
    PASS();
}

TEST(lambda_simple) {
    (void)GLOBALS;
    K x = kcstr("{[x]x+1}");
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r, "tokenization should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KChrType && HDR_COUNT(r) == 1, "should produce 1 token");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(r)[0]), "list should be in CONST range");
    ASSERT(consts && HDR_COUNT(consts) == 1, "should add lambda to consts");
    ASSERT(HDR_TYPE(OBJ_PTR(consts)[0]) == KLambdaType, "const should be KLambdaType");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

TEST(lambda_stores_full_src) {
    (void)GLOBALS;
    const char *src = "{[x]x+1}";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && consts, "tokenization should succeed");
    K lambda = OBJ_PTR(consts)[0];
    ASSERT(is_valid_lambda(lambda), "should be valid lambda");
    K lambda_src = OBJ_PTR(lambda)[3];
    ASSERT(HDR_COUNT(lambda_src) == strlen(src), "source length should match");
    ASSERT(memcmp(CHR_PTR(lambda_src), src, strlen(src)) == 0, "should store full lambda source with braces and params");
    unref(x); unref(r); unref(vars); unref(consts);
    PASS();
}

TEST(lambda_nested) {
    (void)GLOBALS;
    const char *src = "{[x;y]y+{[a]a+1}x}";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r && consts, "tokenization should succeed");
    K outer = OBJ_PTR(consts)[0];
    ASSERT(is_valid_lambda(outer), "outer should be valid lambda");
    ASSERT(HDR_COUNT(OBJ_PTR(outer)[1]) == 2, "outer should have 2 params");
    // Inner lambda should be in outer's consts
    K outer_consts = OBJ_PTR(outer)[2];
    ASSERT(outer_consts && HDR_TYPE(outer_consts) == KObjType, "outer should have consts");
    // Find the inner lambda in consts
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
    (void)GLOBALS;
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
    (void)GLOBALS;
    K r = tokenize("123+456");
    ASSERT(r && HDR_COUNT(r) == 3, "binary op should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 1, "+ should be operator 1 (OPS[:+-*...])");
    unref(r);
    PASS();
}

TEST(unary_plus) {
    (void)GLOBALS;
    K r = tokenize("+123");
    ASSERT(r && HDR_COUNT(r) == 2, "unary op should produce 2 tokens");
    ASSERT(CHR_PTR(r)[0] == 1, "+ should be operator 1");
    unref(r);
    PASS();
}

TEST(assignment) {
    (void)GLOBALS;
    K r = tokenize("a:42");
    ASSERT(r && HDR_COUNT(r) == 3, "assignment should produce 3 tokens");
    ASSERT(CHR_PTR(r)[1] == 0, ": should be operator 0");
    unref(r);
    PASS();
}

TEST(multiple_variables) {
    (void)GLOBALS;
    K r = tokenize("x y z");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 variable tokens");
    unref(r);
    PASS();
}

TEST(paren_token_passthrough) {
    (void)GLOBALS;
    K r = tokenize("(1)");
    ASSERT(r && HDR_COUNT(r) == 3, "should produce 3 tokens");
    ASSERT(CHR_PTR(r)[0] == '(', "first should be literal (");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(r)[1]), "second should be CONST");
    ASSERT(CHR_PTR(r)[2] == ')', "third should be literal )");
    unref(r);
    PASS();
}

// Compilation tests
TEST(compile_empty) {
    (void)GLOBALS;
    K tokens = tokenize("");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 0, "empty should compile to empty");
    unref(bytecode);
    PASS();
}

TEST(compile_constant) {
    (void)GLOBALS;
    K tokens = tokenize("42");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "constant should compile to 1 byte");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST instruction");
    unref(bytecode);
    PASS();
}

TEST(compile_binary_op) {
    (void)GLOBALS;
    K tokens = tokenize("1+2");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "binary op should compile to 3 bytes");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should be PUSH const");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 1), "third should be BINARY ADD");
    unref(bytecode);
    PASS();
}

TEST(compile_unary_op) {
    (void)GLOBALS;
    K tokens = tokenize("+5");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 2, "unary op should compile to 2 bytes");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(CHR_PTR(bytecode)[1] == (OP_UNARY + 1), "second should be UNARY ADD");
    unref(bytecode);
    PASS();
}

TEST(compile_assignment) {
    (void)GLOBALS;
    K vars = 0, consts = 0;
    const char *src = "a:42";
    K x = kcstr(src);
    K tokens = token(x, &vars, &consts);
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode, "assignment should compile");
    ASSERT(ISCLASS(OP_SET_VAR, CHR_PTR(bytecode)[HDR_COUNT(bytecode)-1]), "last instruction should be SET_VAR class");
    unref(x), unref(bytecode); unref(vars); unref(consts);
    PASS();
}

TEST(compile_application) {
    (void)GLOBALS;
    K tokens = tokenize("f x");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "application should compile");
    ASSERT(ISCLASS(OP_GET_VAR, CHR_PTR(bytecode)[0]), "first should be GET var");
    ASSERT(ISCLASS(OP_GET_VAR, CHR_PTR(bytecode)[1]), "second should be GET var");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 5), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

TEST(compile_application2) {
    (void)GLOBALS;
    K tokens = tokenize("\"abc\" 0");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 3, "application should compile");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "first should be PUSH const");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[1]), "second should PUSH const");
    ASSERT(CHR_PTR(bytecode)[2] == (OP_BINARY + 5), "third should be BINARY @ (apply)");
    unref(bytecode);
    PASS();
}

TEST(lambda_postfix_single_arg) {
    (void)GLOBALS;
    const char *src = "{[x]x+1}[6]";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");

    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode && !IS_TAG(bytecode), "compilation should succeed");

    K_char *bc = CHR_PTR(bytecode);
    K_int n = HDR_COUNT(bytecode);

    ASSERT(n == 3, "bytecode should have 3 instructions");
    ASSERT(ISCLASS(OP_CONST, bc[0]), "first should load lambda");
    ASSERT(ISCLASS(OP_CONST, bc[1]), "second should load arg 6");
    ASSERT(bc[2] == OP_N_ARY + 1, "third should be N_ARY apply with 1 arg");

    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(lambda_postfix_two_args) {
    (void)GLOBALS;
    const char *src = "{[x;y]x+y}[1;6]";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K tokens = token(x, &vars, &consts);
    ASSERT(tokens, "tokenization should succeed");

    K bytecode = compile(0, tokens, 0);
    ASSERT(bytecode, "compilation should succeed");

    K_char *bc = CHR_PTR(bytecode);
    K_int n = HDR_COUNT(bytecode);

    ASSERT(n == 4, "bytecode should have 4 instructions");
    ASSERT(ISCLASS(OP_CONST, bc[0]), "first should load lambda");
    ASSERT(ISCLASS(OP_CONST, bc[1]), "second should load arg 1");
    ASSERT(ISCLASS(OP_CONST, bc[2]), "third should load arg 6");
    ASSERT(bc[3] == OP_N_ARY + 2, "fourth should be N_ARY apply with 2 args");

    unref(x), unref(bytecode), unref(vars), unref(consts);
    PASS();
}

TEST(paren_compile_simple) {
    (void)GLOBALS;
    K tokens = tokenize("(1)");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "should compile to 1 byte");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(paren_compile_nested) {
    (void)GLOBALS;
    K tokens = tokenize("((1))");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 1, "nested parens should compile to 1 byte");
    ASSERT(ISCLASS(OP_CONST, CHR_PTR(bytecode)[0]), "should be CONST");
    unref(bytecode);
    PASS();
}

TEST(paren_compile_with_op) {
    (void)GLOBALS;
    K tokens = tokenize("(1+2)*3");
    K bytecode = compile_tokens(tokens);
    ASSERT(bytecode && HDR_COUNT(bytecode) == 5, "should compile to 5 bytes");
    // Bytecode is reversed: 3, (1+2), *, where (1+2) expands to 2, 1, +
    // So: CONST(3), CONST(2), CONST(1), BINARY(+), BINARY(*)
    ASSERT(CHR_PTR(bytecode)[4] == OP_BINARY + 3, "last should be BINARY *");
    unref(bytecode);
    PASS();
}

// VM execution tests
TEST(vm_simple_add) {
    const char *src = "1+2";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "result should be a tag");
    ASSERT(TAG_VAL(result) == 3, "1+2 should evaluate to 3");
    unref(result);
    PASS();
}

TEST(vm_simple_multiply) {
    const char *src = "3*4";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "result should be a tag");
    ASSERT(TAG_VAL(result) == 12, "3*4 should evaluate to 12");
    unref(result);
    PASS();
}

TEST(vm_assignment) {
    const char *src = "x:42";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result == knull(), "assignment as final op should return knull");
    // Check that x was actually assigned
    K x_val = getGlobal(GLOBALS, encodeSym("x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 42, "x should be assigned 42");
    unref(x_val);
    PASS();
}
TEST(vm_assignment_2) {
    const char *src = "x:42+1";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result == knull(), "assignment as final op should return knull");
    // Check that x was actually assigned
    K x_val = getGlobal(GLOBALS, encodeSym("x", 1));
    ASSERT(x_val && TAG_VAL(x_val) == 43, "x should be assigned 43");
    unref(x_val);
    PASS();
}

TEST(index_str_with_atom){
    //(void)GLOBALS;
    const char *src = "\"abc\" 0";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result) && TAG_TYPE(result) == KChrType, "string index should return KChrType atom");
    ASSERT(TAG_VAL(result) == 'a', "string index should return 'a'");
    PASS();
}

TEST(index_str_with_list){
    const char *src = "\"abc\" 2 1 0";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && !IS_TAG(result), "indexing with list should return list");
    ASSERT(HDR_TYPE(result) == KChrType, "result should be KChrType");
    ASSERT(HDR_COUNT(result) == 3, "result should have length 3");
    ASSERT(memcmp(CHR_PTR(result), "cba", 3) == 0, "result should be \"cba\"");
    unref(result);
    PASS();
}

TEST(index_int_with_list){
    const char *src = "3 2 1@0";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "indexing at 0 should return atom");
    ASSERT(TAG_TYPE(result) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(result) == 3, "first element should be 3");
    unref(result);
    PASS();
}

TEST(index_int_out_of_bounds){
    const char *src = "1 2@3";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "out of bounds index should return atom");
    ASSERT(TAG_TYPE(result) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(result) == 0, "out of bounds int index should return 0");
    unref(result);
    PASS();
}

TEST(index_str_out_of_bounds){
    const char *src = "\"ab\"@3";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "out of bounds index should return atom");
    ASSERT(TAG_TYPE(result) == KChrType, "result should be KChrType");
    ASSERT(TAG_VAL(result) == ' ', "out of bounds string index should return space");
    unref(result);
    PASS();
}

TEST(index_postfix_two_args){
    const char *src = "(1 2;3 4)[1;0]";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result), "result should be an atom");
    ASSERT(TAG_TYPE(result) == KIntType, "result should be KIntType");
    ASSERT(TAG_VAL(result) == 3, "result should be 3");
    PASS();
}

TEST(index_postfix_three_args){
    K r = eval(kcstr("((1 2;3 4);(5 6;7 8))[1;0;1]"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 6, "should be 6");
    PASS();
}

TEST(index_postfix_single_arg){
    K r = eval(kcstr("1 2 3[0]"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 1, "should be 1");
    PASS();
}

TEST(apply_oob_multi_arg){
    K r = eval(kcstr("(1;\"ab\")[1;5]"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_TYPE(r) == KChrType, "should be char");
    ASSERT(TAG_VAL(r) == ' ', "OOB string index should be space");
    PASS();
}

TEST(apply_atom_rank_error){
    K r = eval(kcstr("42[0]"), GLOBALS);
    ASSERT(!r && kerrno == KERR_RANK, "atom apply should be rank error");
    PASS();
}

TEST(apply_cascade_rank_error){
    K r = eval(kcstr("1 2 3[0;0]"), GLOBALS);
    ASSERT(!r && kerrno == KERR_RANK, "indexing atom should be rank error");
    PASS();
}

TEST(apply_string_cascade_rank_error){
    K r = eval(kcstr("\"abc\"[1;0]"), GLOBALS);
    ASSERT(!r && kerrno == KERR_RANK, "indexing char atom should be rank error");
    PASS();
}

TEST(apply_too_many_args_rank_error){
    K r = eval(kcstr("(1 2;3 4)[0;0;0]"), GLOBALS);
    ASSERT(!r && kerrno == KERR_RANK, "3 args on 2-deep should be rank error");
    PASS();
}

TEST(apply_chained_bracket_rank_error){
    K r = eval(kcstr("{[x]x+1}[2][0]"), GLOBALS);
    ASSERT(!r && kerrno == KERR_RANK, "indexing lambda result atom should be rank error");
    PASS();
}

TEST(lambda_eval_returns_lambda) {
    K r = eval(kcstr("{[x]x+1}"), GLOBALS);
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    unref(r);
    PASS();
}

TEST(lambda_eval_no_params) {
    K r = eval(kcstr("{[]1}"), GLOBALS);
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    unref(r);
    PASS();
}

TEST(lambda_eval_single_param) {
    K r = eval(kcstr("{[x]x}"), GLOBALS);
    ASSERT(is_valid_lambda(r), "should be valid lambda");
    ASSERT(HDR_COUNT(OBJ_PTR(r)[1]) == 1, "should have 1 param");
    unref(r);
    PASS();
}

TEST(lambda_eval_multi_params) {
    K r = eval(kcstr("{[a;b;c]a+b+c}"), GLOBALS);
    ASSERT(r, "eval should succeed");
    ASSERT(!IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "should return KLambdaType");
    ASSERT(HDR_COUNT(OBJ_PTR(r)[1]) == 3, "should have 3 params");
    unref(r);
    PASS();
}

// Lambda application tests
TEST(lambda_apply_empty_body) {
    K r = eval(kcstr("{[x]}@42"), GLOBALS);
    ASSERT(r == knull(), "lambda with empty body should return knull");
    PASS();
}

TEST(lambda_apply_identity) {
    K r = eval(kcstr("{[x]x}@42"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 42, "{[x]x}@42 should be 42");
    unref(r);
    PASS();
}

TEST(lambda_apply_add) {
    K r = eval(kcstr("{[x]x+1}@2"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 3, "{[x]x+1}@2 should be 3");
    unref(r);
    PASS();
}

TEST(lambda_apply_multiply) {
    K r = eval(kcstr("{[x]x*2}@10"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 20, "{[x]x*2}@10 should be 20");
    unref(r);
    PASS();
}

TEST(lambda_apply_with_local) {
    K r = eval(kcstr("{[x]y:x+1}@6"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 7, "lambda with local: {[x]y:x+1;y}@5 should be 6");
    unref(r);
    PASS();
}

TEST(lambda_apply_multiple_locals) {
    K r = eval(kcstr("{[x]z:y:x+1}@6"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 7, "multiple locals: (5+1)*2=12");
    unref(r);
    PASS();
}

TEST(lambda_apply_local_and_param) {
    K r = eval(kcstr("{[x]x+y:x+1}@5"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 11, "local+param: (5+1)+5=11");
    unref(r);
    PASS();
}

TEST(lambda_apply_nested) {
    K r = eval(kcstr("{[x]x+{[y]y*2}@3}@5"), GLOBALS);
    ASSERT(r && TAG_VAL(r) == 11, "nested lambda: 5+(3*2)=11");
    unref(r);
    PASS();
}

TEST(lambda_apply_no_params) {
    K r = eval(kcstr("{[]42}"), GLOBALS);
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KLambdaType, "{[]42} should eval to lambda");
    unref(r);
    PASS();
}

TEST(lambda_reassign_param) {
    K r = eval(kcstr("{[x]x:\"new\"}@\"old\""), GLOBALS);
    // If double-free, likely crashes or fails leak test
    unref(r);
    PASS();
}

TEST(lambda_error_type_mismatch) {
    K r = eval(kcstr("{[x]x+1}@\"a\""), GLOBALS);
    ASSERT(!r, "applying lambda to string should fail");
    ASSERT(kerrno == KERR_TYPE, "should raise type error");
    PASS();
}

TEST(lambda_error_undefined_var) {
    K r = eval(kcstr("{[x]x+z}@5"), GLOBALS);
    ASSERT(!r, "undefined variable in lambda body should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise value error");
    PASS();
}

TEST(paren_eval_simple) {
    K r = eval(kcstr("(42)"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 42, "(42) should be 42");
    PASS();
}

TEST(paren_eval_grouping) {
    K r = eval(kcstr("(1+2)*3"), GLOBALS);
    ASSERT(r && IS_TAG(r), "result should be a tag");
    ASSERT(TAG_VAL(r) == 9, "(1+2)*3 should be 9");
    PASS();
}

TEST(paren_eval_nested) {
    K r = eval(kcstr("((1+2))"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "((1+2)) should be 3");
    PASS();
}

TEST(paren_eval_deep) {
    K r = eval(kcstr("(((1+2)))"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 3, "(((1+2))) should be 3");
    PASS();
}

TEST(paren_eval_multiple) {
    K r = eval(kcstr("(1+2)+(3+4)"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 10, "(1+2)+(3+4) should be 10");
    PASS();
}

TEST(semicolon_terminated_expr) {
    (void)GLOBALS;
    K r = eval(kcstr("1 2;"), GLOBALS);
    ASSERT(r == knull(), "semicolon-terminated expression should return knull");
    PASS();
}

TEST(multiexpr_basic) {
    (void)GLOBALS;
    K r = eval(kcstr("1 2;2"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 2, "should return 2");
    PASS();
}

TEST(subexpr_with_ops) {
    (void)GLOBALS;
    K r = eval(kcstr("1+2;3*4"), GLOBALS);
    ASSERT(r && IS_TAG(r) && TAG_VAL(r) == 12, "1+2;3*4 should return 12");
    PASS();
}

TEST(subexpr_assignment) {
    const char *src = "x:1;x+2";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result && IS_TAG(result) && TAG_VAL(result) == 3, "x:1;x+2 should return 3");
    unref(result);
    PASS();
}

TEST(fenced_subexpr_basic) {
    K r = eval(kcstr("(1;2)"), GLOBALS);
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 2, "(1;2) should be 1 2");
    unref(r);
    PASS();
}

TEST(fenced_subexpr_with_ops) {
    K r = eval(kcstr("(1;2+3)"), GLOBALS);
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KIntType, "(1;2+3) should return int array");
    ASSERT(HDR_COUNT(r) == 2 && INT_PTR(r)[0] == 1 && INT_PTR(r)[1] == 5, "(1;2+3) should be 1 5");
    unref(r);
    PASS();
}

TEST(fenced_subexpr_heterogeneous) {
    K r = eval(kcstr("(1;\"a\")"), GLOBALS);
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KObjType, "(1;\"a\") should return generic list");
    ASSERT(HDR_COUNT(r) == 2, "(1;\"a\") should have 2 elements");
    ASSERT(TAG_VAL(OBJ_PTR(r)[0]) == 1, "first element should be 1");
    ASSERT(TAG_VAL(OBJ_PTR(r)[1]) == 'a', "second element should be 'a'");
    unref(r);
    PASS();
}

// Error tests
TEST(unclosed_string) {
    (void)GLOBALS;
    const char *src = "\"hello";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r == 0, "unclosed string should fail");
    ASSERT(kerrno == KERR_PARSE, "unclosed string should raise parse error");
    unref(x), unref(vars); unref(consts);
    PASS();
}

TEST(parse_unclosed_string_in_expr) {
    (void)GLOBALS;
    const char *src = "1+2+\"hello";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r == 0, "unclosed string in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    unref(x), unref(vars); unref(consts);
    PASS();
}

TEST(parse_invalid_token_in_expr) {
    (void)GLOBALS;
    const char *src = "1+£+2";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r == 0, "invalid token in expression should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    unref(x), unref(vars); unref(consts);
    PASS();
}

TEST(parse_single_quote) {
    (void)GLOBALS;
    const char *src = "\"";
    K x = kcstr(src);
    K vars = 0, consts = 0;
    K r = token(x, &vars, &consts);
    ASSERT(r == 0, "single quote should fail");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    unref(x), unref(vars); unref(consts);
    PASS();
}

TEST(empty_string_literal) {
    (void)GLOBALS;
    const char *src = "\"\"";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result != 0, "empty string should be valid");
    ASSERT(HDR_TYPE(result) == KChrType, "should be KChrType");
    ASSERT(HDR_COUNT(result) == 0, "should have length 0");
    unref(result);
    PASS();
}

TEST(value_undefined_in_expr) {
    const char *src = "1+foo+2";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result == 0, "undefined variable in expression should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise KERR_VALUE");
    PASS();
}

TEST(vm_undefined_variable) {
    const char *src = "foo";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result == 0, "undefined variable should return error");
    ASSERT(kerrno == KERR_VALUE, "undefined variable should raise value error");
    PASS();
}

TEST(lambda_error_missing_bracket) {
    K r = eval(kcstr("{x}"), GLOBALS);
    ASSERT(!r, "should error: lambda must have params");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(lambda_error_unclosed_params) {
    K r = eval(kcstr("{[x"), GLOBALS);
    ASSERT(!r, "should error: unclosed param list");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(lambda_error_unclosed_lambda) {
    K r = eval(kcstr("{[x]x+1"), GLOBALS);
    ASSERT(!r, "should error: unclosed lambda");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

TEST(error_unmatched_paren) {
    K r = eval(kcstr("(1+2"), GLOBALS);
    ASSERT(!r, "unmatched paren should error");
    ASSERT(kerrno == KERR_PARSE, "should raise KERR_PARSE");
    PASS();
}

// Monad tests
TEST(monad_value_basic) {
    K r = eval(kcstr(".\"tests/read.txt\""), GLOBALS);
    ASSERT(r && !IS_TAG(r) && HDR_TYPE(r) == KChrType, "should read file as KChrType");
    ASSERT(HDR_COUNT(r) == 5, "file should have 5 chars");
    ASSERT(memcmp(CHR_PTR(r), "hello", 5) == 0, "content should be 'hello'");
    unref(r);
    PASS();
}

TEST(monad_value_file_not_found) {
    K r = eval(kcstr(".\"nonexistent_file_12345.txt\""), GLOBALS);
    ASSERT(!r, "missing file should fail");
    ASSERT(kerrno == KERR_VALUE, "should raise KERR_VALUE");
    PASS();
}

TEST(monad_value_type_error) {
    K r = eval(kcstr(". 123"), GLOBALS);
    ASSERT(!r, "value on integer should fail");
    ASSERT(kerrno == KERR_NYI, "should raise KERR_NYI");
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
    RUN_TEST(lambda_error_type_mismatch);
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

    printf("\nMonads:\n");
    RUN_TEST(monad_value_basic);
    RUN_TEST(monad_value_file_not_found);
    RUN_TEST(monad_value_type_error);

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