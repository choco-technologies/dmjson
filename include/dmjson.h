#ifndef DMJSON_H
#define DMJSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dmod_types.h"
#include "dmjson_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DMJSON_OK                0
#define DMJSON_ERR_INVALID      -1
#define DMJSON_ERR_MEMORY       -2
#define DMJSON_ERR_SYNTAX       -3
#define DMJSON_ERR_NOT_FOUND    -4
#define DMJSON_ERR_TYPE         -5
#define DMJSON_ERR_BUFFER       -6
#define DMJSON_ERR_FILE         -7
#define DMJSON_ERR_LIMIT        -8

/* Compile-time resource limits; no arrays of these sizes are reserved. */
#ifndef DMJSON_MAX_DEPTH
#define DMJSON_MAX_DEPTH 64u
#endif
#ifndef DMJSON_MAX_NODES
#define DMJSON_MAX_NODES 4096u
#endif

typedef struct dmjson* dmjson_t;
typedef dmjson_t dmjson_context_t;
typedef enum {
    DMJSON_MISSING = 0, DMJSON_NULL, DMJSON_BOOL, DMJSON_NUMBER,
    DMJSON_STRING, DMJSON_ARRAY, DMJSON_OBJECT
} dmjson_type_t;

/** Owns its tree. An empty context has no root. NULL destruction is safe. */
dmod_dmjson_api(1.0, dmjson_t, _create, (void));
dmod_dmjson_api(1.0, void, _destroy, (dmjson_t ctx));
dmod_dmjson_api(1.0, bool, _is_valid, (dmjson_t ctx));

/** Strict JSON/UTF-8. Input is copied. Failure preserves the previous tree.
 * parse_buffer accepts exactly length bytes (no terminating NUL required).
 * error_offset is a byte offset into the last parsed input, SIZE_MAX if none.
 */
dmod_dmjson_api(1.0, int, _parse_string, (dmjson_t ctx, const char* json));
dmod_dmjson_api(1.0, int, _parse_buffer, (dmjson_t ctx, const char* json, size_t length));
dmod_dmjson_api(1.0, int, _parse_file, (dmjson_t ctx, const char* filename));
dmod_dmjson_api(1.0, size_t, _error_offset, (dmjson_t ctx));

/** Compact output. Returns bytes INCLUDING NUL, or a negative error.
 * buffer == NULL queries size. Short buffers remain untouched.
 */
dmod_dmjson_api(1.0, int, _generate_string, (dmjson_t ctx, char* buffer, size_t capacity));
dmod_dmjson_api(1.0, int, _generate_file, (dmjson_t ctx, const char* filename));

/** Paths use RFC 6901 JSON Pointer: "" = root, "/a/0" = first array item.
 * ~0 decodes to ~, ~1 to /. Arrays require canonical decimal indexes.
 * Missing/wrong-type getters return MISSING, 0, NULL or the supplied fallback.
 * Returned pointers are borrowed until the next successful mutation/destruction.
 * Strings may contain NUL: use string_length; keys likewise use key_length.
 * Numbers retain their exact JSON spelling; get_int accepts only int32 literals.
 */
dmod_dmjson_api(1.0, dmjson_type_t, _type, (dmjson_t ctx, const char* path));
dmod_dmjson_api(1.0, size_t, _count, (dmjson_t ctx, const char* path));
dmod_dmjson_api(1.0, const char*, _key_name, (dmjson_t ctx, const char* path, size_t index));
dmod_dmjson_api(1.0, size_t, _key_length, (dmjson_t ctx, const char* path, size_t index));
dmod_dmjson_api(1.0, const char*, _get_string, (dmjson_t ctx, const char* path, const char* fallback));
dmod_dmjson_api(1.0, size_t, _string_length, (dmjson_t ctx, const char* path));
dmod_dmjson_api(1.0, const char*, _get_number, (dmjson_t ctx, const char* path, const char* fallback));
dmod_dmjson_api(1.0, int32_t, _get_int, (dmjson_t ctx, const char* path, int32_t fallback));
dmod_dmjson_api(1.0, bool, _get_bool, (dmjson_t ctx, const char* path, bool fallback));

/** Set replaces an existing value or adds an object member. Parent must exist.
 * Array "-" appends; an existing index replaces (no implicit insertion).
 * set_json accepts any JSON value; set_number accepts only a JSON number.
 * Failures preserve the tree. New values and keys are copied.
 * remove("") clears the context. No internal synchronization is provided.
 */
dmod_dmjson_api(1.0, int, _set_json, (dmjson_t ctx, const char* path, const char* json));
dmod_dmjson_api(1.0, int, _set_string, (dmjson_t ctx, const char* path, const char* value));
dmod_dmjson_api(1.0, int, _set_number, (dmjson_t ctx, const char* path, const char* value));
dmod_dmjson_api(1.0, int, _set_int, (dmjson_t ctx, const char* path, int32_t value));
dmod_dmjson_api(1.0, int, _set_bool, (dmjson_t ctx, const char* path, bool value));
dmod_dmjson_api(1.0, int, _set_null, (dmjson_t ctx, const char* path));
dmod_dmjson_api(1.0, int, _remove, (dmjson_t ctx, const char* path));

#ifdef __cplusplus
}
#endif
#endif
