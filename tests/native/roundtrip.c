#define _POSIX_C_SOURCE 200809L
#include "dmod.h"
#include "dmjson.h"
#include "sal.h"
#include <stdlib.h>

/* Line protocol used only by the Python differential test. getline's buffer is
 * dynamically allocated, as is every generated result. */
int main(void)
{
    dmjson_t ctx = dmjson_create();
    char *input = NULL, *out;
    size_t capacity = 0;
    int size;
    if (!ctx) return 1;
    while (getline(&input, &capacity, stdin) >= 0) {
        if (dmjson_parse_string(ctx, input)) { puts("ERROR"); continue; }
        size = dmjson_generate_string(ctx, NULL, 0);
        if (size < 0) return 1;
        out = malloc((size_t)size);
        if (!out || dmjson_generate_string(ctx, out, (size_t)size) != size) return 1;
        puts(out);
        free(out);
    }
    free(input);
    dmjson_destroy(ctx);
    return native_live_blocks ? 1 : 0;
}
