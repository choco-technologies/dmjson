#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmjson.h"
#include <limits.h>

static dmjson_t g_handle;

static bool same(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

void dmod_test_setup(void) { g_handle = dmjson_create(); }
void dmod_test_teardown(void) { dmjson_destroy(g_handle); g_handle = NULL; }

DMOD_TEST_STEP(lifecycle)
{
    DMOD_TEST_EXPECT_TRUE(dmjson_is_valid(g_handle));
    DMOD_TEST_EXPECT_FALSE(dmjson_is_valid(NULL));
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, ""), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_string(g_handle, NULL, 0), DMJSON_ERR_NOT_FOUND);
    dmjson_destroy(NULL);
}

DMOD_TEST_STEP(types_and_nested_values)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle,
        " {\"a\":[null,true,false,42,-17,1.23e+45,\"hello\",{},[]]} \r\n"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, ""), DMJSON_OBJECT);
    DMOD_TEST_EXPECT_EQ(dmjson_count(g_handle, "/a"), 9);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/0"), DMJSON_NULL);
    DMOD_TEST_EXPECT_TRUE(dmjson_get_bool(g_handle, "/a/1", false));
    DMOD_TEST_EXPECT_FALSE(dmjson_get_bool(g_handle, "/a/2", true));
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a/3", 0), 42);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a/4", 0), -17);
    DMOD_TEST_EXPECT_TRUE(same(dmjson_get_number(g_handle, "/a/5", NULL), "1.23e+45"));
    DMOD_TEST_EXPECT_TRUE(same(dmjson_get_string(g_handle, "/a/6", NULL), "hello"));
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/7"), DMJSON_OBJECT);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/8"), DMJSON_ARRAY);
    DMOD_TEST_EXPECT_EQ(dmjson_error_offset(g_handle), SIZE_MAX);
}

DMOD_TEST_STEP(pointer_escaping)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle,
        "{\"\":1,\"a/b\":{\"~key\":2},\"a\":[10,20],\"01\":3}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/", 0), 1);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a~1b/~0key", 0), 2);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/01", 0), 3);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/01"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/-"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a/184467440737095516160"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "a"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/a~2b"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/bad~", 3), DMJSON_ERR_INVALID);
}

DMOD_TEST_STEP(unicode_and_nul)
{
    const char *s;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle,
        "{\"k\\u0000x\":\"A\\u0000B\\u00e9\\ud83d\\ude00\",\"a\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_key_length(g_handle, "", 0), 3);
    s = dmjson_key_name(g_handle, "", 0);
    DMOD_TEST_EXPECT_NOT_NULL(s);
    if (s) { DMOD_TEST_EXPECT_EQ(s[1], 0); DMOD_TEST_EXPECT_EQ(s[2], 'x'); }
    /* Embedded-NUL keys can be enumerated, but cannot be addressed by C-string pointers. */
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "\"A\\u0000B\\u00e9\\ud83d\\ude00\""), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_string_length(g_handle, ""), 9);
    s = dmjson_get_string(g_handle, "", NULL);
    if (s) {
        DMOD_TEST_EXPECT_EQ(s[0], 'A'); DMOD_TEST_EXPECT_EQ(s[1], 0);
        DMOD_TEST_EXPECT_EQ(s[2], 'B'); DMOD_TEST_EXPECT_EQ((unsigned char)s[3], 0xc3);
        DMOD_TEST_EXPECT_EQ((unsigned char)s[5], 0xf0);
    } else DMOD_TEST_FAIL();
}

DMOD_TEST_STEP(integers_and_exact_numbers)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle,
        "[-2147483648,2147483647,-2147483649,2147483648,1.0,1e0,-0,123456789012345678901234567890]"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/0", 9), INT32_MIN);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/1", 9), INT32_MAX);
    for (unsigned i = 2; i < 6; ++i) {
        char path[3] = {'/', (char)('0' + i), 0};
        DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, path, 9), 9);
    }
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/6", 9), 0);
    DMOD_TEST_EXPECT_TRUE(same(dmjson_get_number(g_handle, "/7", NULL), "123456789012345678901234567890"));
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/0", INT32_MIN), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/0", 9), INT32_MIN);
}

static void reject(const char *json)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, json), DMJSON_ERR_SYNTAX);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "", 0), 7);
    DMOD_TEST_EXPECT_NE(dmjson_error_offset(g_handle), SIZE_MAX);
}

DMOD_TEST_STEP(invalid_syntax_is_atomic)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "7"), DMJSON_OK);
    reject(""); reject(" "); reject("["); reject("{"); reject("[1,]");
    reject("{\"a\":1,}"); reject("{a:1}"); reject("{\"a\" 1}"); reject("{\"a\":}");
    reject("[1 2]"); reject("[,1]"); reject("{}{}"); reject("true false");
    reject("NaN"); reject("Infinity"); reject("+1"); reject("01"); reject("-01");
    reject("1."); reject("1e"); reject("1e+"); reject("--1"); reject(".1");
    reject("\"unterminated"); reject("\"\\x00\""); reject("\"\n\"");
    reject("\"\\ud800\""); reject("\"\\udfff\""); reject("\"\\ud800\\u0041\"");
    reject("\"\\u12xx\""); reject("\"\xc0\x80\""); reject("\"\xed\xa0\x80\"");
    reject("\"\xf4\x90\x80\x80\""); reject("\"\xf0\x9f\""); reject("\"\x80\"");
    reject("/*comment*/1"); reject("[truefalse]"); reject("nul");
}

DMOD_TEST_STEP(bounded_input)
{
    const char raw[4] = {'[', '1', ']', 'x'};
    const char nul[3] = {'1', 0, '2'};
    DMOD_TEST_EXPECT_EQ(dmjson_parse_buffer(g_handle, raw, 3), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/0", 0), 1);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_buffer(g_handle, raw, 4), DMJSON_ERR_SYNTAX);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_buffer(g_handle, nul, 3), DMJSON_ERR_SYNTAX);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_buffer(g_handle, raw, SIZE_MAX), DMJSON_ERR_INVALID);
}

DMOD_TEST_STEP(generation_and_buffers)
{
    char *out;
    int size;
    char short_buffer[3] = {'x', 'y', 'z'};
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, " { \"a\" : [true,{},[],null,\"x\\u0000\\n\"] } "), DMJSON_OK);
    size = dmjson_generate_string(g_handle, NULL, 0);
    DMOD_TEST_EXPECT_TRUE(size > 0);
    if (size <= 0) return;
    out = Dmod_Malloc((size_t)size);
    DMOD_TEST_EXPECT_NOT_NULL(out);
    if (!out) return;
    DMOD_TEST_EXPECT_EQ(dmjson_generate_string(g_handle, short_buffer, sizeof(short_buffer)), DMJSON_ERR_BUFFER);
    DMOD_TEST_EXPECT_EQ(short_buffer[0], 'x'); DMOD_TEST_EXPECT_EQ(short_buffer[2], 'z');
    DMOD_TEST_EXPECT_EQ(dmjson_generate_string(g_handle, out, (size_t)size), size);
    DMOD_TEST_EXPECT_TRUE(same(out, "{\"a\":[true,{},[],null,\"x\\u0000\\u000a\"]}"));
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, out), DMJSON_OK);
    Dmod_Free(out);
}

DMOD_TEST_STEP(mutation)
{
    DMOD_TEST_EXPECT_EQ(dmjson_set_json(g_handle, "", "{\"a\":[1,2],\"o\":{}}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_string(g_handle, "/o/message", "quote\"\\\n"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_bool(g_handle, "/a/-", true), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_null(g_handle, "/a/0"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_number(g_handle, "/a/1", "1e+900"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_json(g_handle, "/new", "{\"b\":[3]}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/new/b/0", 0), 3);
    DMOD_TEST_EXPECT_TRUE(same(dmjson_get_string(g_handle, "/o/message", NULL), "quote\"\\\n"));
    DMOD_TEST_EXPECT_EQ(dmjson_count(g_handle, "/a"), 3);
    DMOD_TEST_EXPECT_TRUE(dmjson_get_bool(g_handle, "/a/2", false));
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/a/1"), DMJSON_OK);
    DMOD_TEST_EXPECT_TRUE(dmjson_get_bool(g_handle, "/a/1", false));
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/a/-", 99), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/a/0"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/a/1"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/new"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/new"), DMJSON_MISSING);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, ""), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, ""), DMJSON_MISSING);
}

DMOD_TEST_STEP(failed_mutation_and_defaults)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "{\"x\":5,\"a\":[1]}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_set_json(g_handle, "/x", "[1,]"), DMJSON_ERR_SYNTAX);
    DMOD_TEST_EXPECT_EQ(dmjson_set_number(g_handle, "/x", "true"), DMJSON_ERR_TYPE);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/missing/a", 1), DMJSON_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/x/a", 1), DMJSON_ERR_TYPE);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/a/1", 1), DMJSON_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(dmjson_set_string(g_handle, "/x", "\xc0\x80"), DMJSON_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(dmjson_set_null(g_handle, "/\xc0\x80"), DMJSON_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/x", 0), 5);
    DMOD_TEST_EXPECT_TRUE(same(dmjson_get_string(g_handle, "/x", "fallback"), "fallback"));
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/missing"), DMJSON_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(NULL, "", 55), 55);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(NULL, "{}"), DMJSON_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, NULL), DMJSON_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(dmjson_set_null(g_handle, NULL), DMJSON_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, NULL), DMJSON_MISSING);
    DMOD_TEST_EXPECT_NULL(dmjson_key_name(g_handle, "/a", 0));
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(NULL, "x"), DMJSON_ERR_INVALID);
}

DMOD_TEST_STEP(duplicate_keys_and_escaped_mutation)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "{\"a\":1,\"a\":2}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_count(g_handle, ""), 2);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a", 0), 2);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/a", 3), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a", 0), 3);
    DMOD_TEST_EXPECT_EQ(dmjson_remove(g_handle, "/a"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a", 0), 1);
    DMOD_TEST_EXPECT_EQ(dmjson_set_int(g_handle, "/a~1b~0c", 4), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a~1b~0c", 0), 4);
    DMOD_TEST_EXPECT_TRUE(same(dmjson_key_name(g_handle, "", 1), "a/b~c"));
}

DMOD_TEST_STEP(depth_limit)
{
    char *text = Dmod_Malloc(2 * (DMJSON_MAX_DEPTH + 1) + 2);
    int size;
    DMOD_TEST_EXPECT_NOT_NULL(text);
    if (!text) return;
    for (size_t i = 0; i < DMJSON_MAX_DEPTH + 1; ++i) text[i] = '[';
    text[DMJSON_MAX_DEPTH + 1] = '0';
    for (size_t i = 0; i < DMJSON_MAX_DEPTH + 1; ++i) text[DMJSON_MAX_DEPTH + 2 + i] = ']';
    text[2 * (DMJSON_MAX_DEPTH + 1) + 1] = 0;
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, text), DMJSON_ERR_LIMIT);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_buffer(g_handle, text + 1, 2 * DMJSON_MAX_DEPTH + 1), DMJSON_OK);
    size = dmjson_generate_string(g_handle, NULL, 0);
    DMOD_TEST_EXPECT_EQ(size, 2 * DMJSON_MAX_DEPTH + 2);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_string(g_handle, text, 2 * (DMJSON_MAX_DEPTH + 1) + 2), size);
    DMOD_TEST_EXPECT_EQ(dmjson_set_json(g_handle, "/0", text), DMJSON_ERR_LIMIT);
    DMOD_TEST_EXPECT_EQ(dmjson_type(g_handle, "/0"), DMJSON_ARRAY);
    Dmod_Free(text);
}

/* Override for systems with a different writable temporary directory. */
#ifndef DMJSON_TEST_FILE_PATH
#define DMJSON_TEST_FILE_PATH "/tmp/dmjson-roundtrip-test.json"
#endif
DMOD_TEST_STEP(file_roundtrip)
{
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "{\"a\":[1,true,\"\\u00e9\"]}"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_generate_file(g_handle, DMJSON_TEST_FILE_PATH), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_string(g_handle, "null"), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_parse_file(g_handle, DMJSON_TEST_FILE_PATH), DMJSON_OK);
    DMOD_TEST_EXPECT_EQ(dmjson_get_int(g_handle, "/a/0", 0), 1);
    DMOD_TEST_EXPECT_TRUE(dmjson_get_bool(g_handle, "/a/1", false));
    DMOD_TEST_EXPECT_EQ(dmjson_string_length(g_handle, "/a/2"), 2);
    DMOD_TEST_EXPECT_EQ(Dmod_FileRemove(DMJSON_TEST_FILE_PATH), 0);
}
