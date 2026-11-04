#include "dmod_test.h"
#include "dmjson.h"
#include "sal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

volatile int dmod_test_step_failed;
static int failed;
void dmod_test_setup(void);
void dmod_test_teardown(void);

#define RUN(name) do { \
    void test_##name(void); \
    dmod_test_step_failed = 0; dmod_test_setup(); \
    test_##name(); dmod_test_teardown(); \
    if (native_live_blocks) { fprintf(stderr, "leak after %s: %zu\n", #name, native_live_blocks); dmod_test_step_failed = 1; } \
    failed += dmod_test_step_failed; \
} while (0)

static const char *document = "{\"a\":[1,2,{\"b\":\"\\ud83d\\ude00\"}],\"x\":false}";

/* Fail every allocation in turn, exercising cleanup and transactional updates. */
static void allocation_failures(void)
{
    dmjson_t ctx;
    size_t baseline, bytes;
    int result = DMJSON_ERR_MEMORY;
    native_fail_after = 0;
    DMOD_TEST_EXPECT_NULL(dmjson_create());
    native_fail_after = -1;
    ctx = dmjson_create();
    DMOD_TEST_EXPECT_NOT_NULL(ctx);
    if (!ctx) return;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, "7"), DMJSON_OK);
    baseline = native_live_blocks; bytes = native_live_bytes;
    for (long i = 0; i < 128; ++i) {
        native_fail_after = i;
        result = dmjson_parse_string(ctx, document);
        native_fail_after = -1;
        if (!result) break;
        DMOD_TEST_EXPECT_EQ(result, DMJSON_ERR_MEMORY);
        DMOD_TEST_EXPECT_EQ(dmjson_get_int(ctx, "", 0), 7);
        DMOD_TEST_EXPECT_EQ(native_live_blocks, baseline);
        DMOD_TEST_EXPECT_EQ(native_live_bytes, bytes);
    }
    DMOD_TEST_EXPECT_EQ(result, DMJSON_OK);
    for (unsigned op = 0; op < 4; ++op) {
        DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, "{\"keep\":7}"), DMJSON_OK);
        baseline = native_live_blocks; bytes = native_live_bytes;
        result = DMJSON_ERR_MEMORY;
        for (long i = 0; i < 128; ++i) {
            native_fail_after = i;
            if (op == 0) result = dmjson_set_json(ctx, "/new", document);
            else if (op == 1) result = dmjson_set_string(ctx, "/new", "hello");
            else if (op == 2) result = dmjson_set_bool(ctx, "/new", true);
            else result = dmjson_set_number(ctx, "/new", "1e99");
            native_fail_after = -1;
            if (!result) break;
            DMOD_TEST_EXPECT_EQ(result, DMJSON_ERR_MEMORY);
            DMOD_TEST_EXPECT_EQ(dmjson_get_int(ctx, "/keep", 0), 7);
            DMOD_TEST_EXPECT_EQ(dmjson_count(ctx, ""), 1);
            DMOD_TEST_EXPECT_EQ(native_live_blocks, baseline);
            DMOD_TEST_EXPECT_EQ(native_live_bytes, bytes);
        }
        DMOD_TEST_EXPECT_EQ(result, DMJSON_OK);
    }
    baseline = native_live_blocks;
    native_fail_after = 0;
    DMOD_TEST_EXPECT_EQ(dmjson_generate_string(ctx, NULL, 0), DMJSON_ERR_MEMORY);
    native_fail_after = -1;
    DMOD_TEST_EXPECT_EQ(native_live_blocks, baseline);
    dmjson_destroy(ctx);
}

static void files_and_errors(void)
{
    dmjson_t ctx = dmjson_create();
    const char *path = "native-test.json";
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, document), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(ctx, path), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, "7"), DMJSON_OK);
    native_read_chunk = 3;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_file(ctx, path), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(ctx, "/a/2/b", 123), 123);
    DMOD_TEST_EXPECT_EQ(dmjson_string_length(ctx, "/a/2/b"), 4);
    native_read_chunk = SIZE_MAX;
    native_read_fail = 1;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_file(ctx, path), DMJSON_ERR_FILE);
    native_read_fail = 0;
    native_write_chunk = 1;
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(ctx, path), DMJSON_ERR_FILE);
    native_write_chunk = SIZE_MAX;
    {
        char *large = malloc(1025);
        memset(large, 'x', 1024); large[1024] = 0;
        DMOD_TEST_EXPECT_EQ(dmjson_set_string(ctx, "", large), DMJSON_OK);
        DMOD_TEST_EXPECT_EQ(dmjson_generate_file(ctx, path), DMJSON_OK);
        DMOD_TEST_EXPECT_EQ(dmjson_parse_file(ctx, path), DMJSON_OK);
        DMOD_TEST_EXPECT_EQ(dmjson_string_length(ctx, ""), 1024);
        DMOD_TEST_EXPECT_EQ(strcmp(dmjson_get_string(ctx, "", ""), large), 0);
        free(large);
    }
    DMOD_TEST_EXPECT_EQ(dmjson_parse_file(ctx, "/no-such-directory/dmjson.json"), DMJSON_ERR_FILE);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(ctx, "/no-such-directory/dmjson.json"), DMJSON_ERR_FILE);
    native_fail_after = 0;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_file(ctx, path), DMJSON_ERR_MEMORY);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(ctx, path), DMJSON_ERR_MEMORY);
    native_fail_after = -1;
    remove(path);
    dmjson_destroy(ctx);
}

static void node_limits(void)
{
    dmjson_t ctx = dmjson_create();
    char *text = malloc(2 * DMJSON_MAX_NODES + 3);
    DMOD_TEST_EXPECT_NOT_NULL(text);
    if (!text) { dmjson_destroy(ctx); return; }
    text[0] = '[';
    for (size_t i = 0; i < DMJSON_MAX_NODES; ++i) {
        text[2 * i + 1] = '0'; text[2 * i + 2] = ',';
    }
    text[2 * DMJSON_MAX_NODES] = ']';
    text[2 * DMJSON_MAX_NODES + 1] = 0;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, text), DMJSON_ERR_LIMIT);
    text[2 * DMJSON_MAX_NODES - 2] = ']';
    text[2 * DMJSON_MAX_NODES - 1] = 0;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, text), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_count(ctx, ""), DMJSON_MAX_NODES - 1);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(ctx, "/-", 1), DMJSON_ERR_LIMIT);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(ctx, "/0", 1), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(ctx, "/0"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_null(ctx, "/-"), DMJSON_OK);
    free(text); dmjson_destroy(ctx);
}

/* Deterministic byte fuzzing plus every truncated prefix of valid input. */
static void fuzz_inputs(void)
{
    dmjson_t ctx = dmjson_create();
    unsigned char *bytes = malloc(512);
    uint32_t seed = 0x12345678;
    int size;
    char *out;
    for (size_t n = 0; n <= strlen(document); ++n) (void)dmjson_parse_buffer(ctx, document, n);
    for (unsigned run = 0; run < 50000; ++run) {
        size_t n = run % 512;
        for (size_t i = 0; i < n; ++i) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            bytes[i] = (unsigned char)seed;
        }
        if (dmjson_parse_buffer(ctx, (const char*)bytes, n)) continue;
        size = dmjson_generate_string(ctx, NULL, 0);
        DMOD_TEST_EXPECT_TRUE(size > 0);
        if (size <= 0) continue;
        out = malloc((size_t)size);
        DMOD_TEST_EXPECT_EQ(dmjson_generate_string(ctx, out, (size_t)size), size);
        DMOD_TEST_EXPECT_EQ(dmjson_parse_string(ctx, out), DMJSON_OK);
        free(out);
    }
    free(bytes); dmjson_destroy(ctx);
}

int main(void)
{
    RUN(lifecycle); RUN(types_and_nested_values); RUN(pointer_escaping);
    RUN(unicode_and_nul); RUN(integers_and_exact_numbers); RUN(invalid_syntax_is_atomic);
    RUN(bounded_input); RUN(generation_and_buffers); RUN(mutation);
    RUN(failed_mutation_and_defaults); RUN(duplicate_keys_and_escaped_mutation); RUN(depth_limit); RUN(file_roundtrip);
    dmod_test_step_failed = 0;
    allocation_failures(); files_and_errors(); node_limits(); fuzz_inputs();
    failed += dmod_test_step_failed;
    if (native_live_blocks || native_live_bytes) { fprintf(stderr, "final leak\n"); ++failed; }
    printf("dmjson native tests: %s\n", failed ? "FAILED" : "PASSED");
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
