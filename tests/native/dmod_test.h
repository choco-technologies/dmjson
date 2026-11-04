#include "dmod.h"
extern volatile int dmod_test_step_failed;
#define DMOD_TEST_STEP(name) void test_##name(void)
#define DMOD_TEST_EXPECT(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    dmod_test_step_failed = 1; } } while (0)
#define DMOD_TEST_EXPECT_TRUE(expr) DMOD_TEST_EXPECT(expr)
#define DMOD_TEST_EXPECT_FALSE(expr) DMOD_TEST_EXPECT(!(expr))
#define DMOD_TEST_EXPECT_EQ(a,b) DMOD_TEST_EXPECT((a) == (b))
#define DMOD_TEST_EXPECT_NE(a,b) DMOD_TEST_EXPECT((a) != (b))
#define DMOD_TEST_EXPECT_NULL(a) DMOD_TEST_EXPECT((a) == NULL)
#define DMOD_TEST_EXPECT_NOT_NULL(a) DMOD_TEST_EXPECT((a) != NULL)
#define DMOD_TEST_FAIL() DMOD_TEST_EXPECT(0)
