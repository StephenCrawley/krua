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

/* Testing Principles & Practices:
 * - Always use `const char *src = "..."` pattern for string literals, never inline them
 * - Use strlen(src) for lengths, never hardcode magic numbers like 3, 4, etc
 * - Cast to (K) when passing to knewcopy: knewcopy(KChrType, strlen(src), (K)src)
 * - Use ISCLASS(class, byte) macro for bytecode range checks, not manual arithmetic
 * - Keep tests simple and direct - no frameworks, just ASSERT and PASS
 * - Error tests will print to stdout (messy output) - this is expected and proves errors are caught
 * - Each test should clean up its allocations with unref()
 * - GLOBALS is now passed as parameter - use it for eval() calls
 * 
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
    return compileExpr(tokens); // NOTE: compileExpr unrefs tokens
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

TEST(vm_undefined_variable) {
    const char *src = "foo";
    K result = eval(kcstr(src), GLOBALS);
    ASSERT(result == 0, "undefined variable should return error");
    ASSERT(kerrno == KERR_VALUE, "undefined variable should raise value error");
    PASS();
}

// Edge case tests
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

// Test runner
void run_tests() {
    printf("\nPreprocessing:\n");
    RUN_TEST(strip_leading_comment);
    RUN_TEST(strip_trailing_comment);
    RUN_TEST(strip_trailing_whitespace);
    RUN_TEST(strip_both);
    //RUN_TEST(ignore_quoted_slash);

    printf("\nTokenization:\n");
    RUN_TEST(empty_input);
    RUN_TEST(single_variable);
    RUN_TEST(multiple_variables);
    RUN_TEST(single_integer);
    RUN_TEST(integer_list);
    RUN_TEST(string_literal);
    RUN_TEST(trailing_whitespace);
    RUN_TEST(binary_add);
    RUN_TEST(unary_plus);
    RUN_TEST(assignment);

    printf("\nCompilation:\n");
    RUN_TEST(compile_empty);
    RUN_TEST(compile_constant);
    RUN_TEST(compile_binary_op);
    RUN_TEST(compile_unary_op);
    RUN_TEST(compile_assignment);
    RUN_TEST(compile_application);

    printf("\nVM Execution:\n");
    RUN_TEST(vm_simple_add);
    RUN_TEST(vm_simple_multiply);
    RUN_TEST(vm_assignment);
    RUN_TEST(vm_assignment_2);

    printf("\nError Handling:\n");
    RUN_TEST(invalid_token);
    RUN_TEST(unclosed_string);
    RUN_TEST(parse_unclosed_string_in_expr);
    RUN_TEST(parse_invalid_token_in_expr);
    RUN_TEST(parse_single_quote);
    RUN_TEST(empty_string_literal);
    RUN_TEST(vm_undefined_variable);
    RUN_TEST(value_undefined_in_expr);

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