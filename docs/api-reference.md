# dmjson API reference

Include `dmjson.h` and dynamically link the `dmjson` module interface. All public
functions use API version 1.0. `dmjson_t` (alias `dmjson_context_t`) is an opaque,
heap-owned context. There are no global document buffers or caches.

## Lifetime and concurrency

`dmjson_create()` returns an empty context or NULL on allocation failure.
`dmjson_is_valid()` tests a non-NULL live handle's magic tag.
`dmjson_destroy()` frees the whole tree and context; NULL is allowed. As with
other C handles, arbitrary or already freed pointers must never be supplied;
the magic field does not make freed memory safe to inspect.

Each context owns its keys, decoded strings and number literals. Input storage
can be reused immediately after a successful parse/set. Getters return borrowed
pointers: conservatively treat them as invalid after any successful parse, edit
or destruction. Callers must serialize access to the same context. Independent
contexts share only the platform SAL allocator/file services.

## Parse and generate

| Function | Behavior |
| --- | --- |
| `parse_string(ctx, json)` | Parse one NUL-terminated JSON value. |
| `parse_buffer(ctx, json, length)` | Parse exactly `length` readable bytes; no terminator required. |
| `parse_file(ctx, filename)` | Read a sized file with SAL, then parse. Short reads are retried; premature EOF is a file error. |
| `error_offset(ctx)` | Byte offset for the last parser error, or `SIZE_MAX` after successful parsing/no diagnostic. |
| `generate_string(ctx, buffer, capacity)` | Compact JSON; returns required bytes **including NUL**. NULL buffer queries size. |
| `generate_file(ctx, filename)` | Write compact JSON without NUL through SAL; returns `DMJSON_OK`. |

Parse operations accept any root type. Surrounding JSON whitespace is allowed;
trailing data, BOMs, comments, trailing commas, invalid numbers and malformed
UTF-8/Unicode escapes are rejected. Strings decode into UTF-8; escaped NUL is
preserved. Numbers retain their exact original spelling, including exponents,
large integers and negative zero. No floating-point library is required.

Parse failures leave the previous tree intact. Syntax offsets identify where
parsing stopped, which may be just after the offending byte. Allocation failure
before parser creation leaves the previous diagnostic unchanged. Offsets also
refer to the fragment most recently supplied to `set_json`/`set_number`.

Short output buffers are untouched; the required size can be queried first.
An empty context generates `ERR_NOT_FOUND`; set/parse a root value first.
Output lengths exceeding `INT_MAX` return `ERR_LIMIT`. File writing may leave
a partial/truncated file on I/O failure; use a temporary path and application
rename if transactional disk updates are required. SAL's void close API cannot
report delayed close/flush errors. File parsing requires SAL `FileSize` support
and temporarily allocates the whole source file; streams are not supported.

## Paths and inspection

Paths follow JSON Pointer: `""` is root, `/network/port` an object member,
`/items/0` an array item. `~0` means `~`, `~1` means `/`. `/` addresses an empty
object key. Array indexes have no signs or leading zeros (except `0` itself).
An overflowing index is rejected. NULL is not a root-path alias.

| Function | Result |
| --- | --- |
| `type(ctx, path)` | `DMJSON_NULL`, `BOOL`, `NUMBER`, `STRING`, `ARRAY`, `OBJECT`, or `DMJSON_MISSING`. |
| `count(ctx, path)` | Number of immediate children for containers, otherwise 0. |
| `key_name(ctx, path, index)` | Object key at insertion-order index, or NULL. |
| `key_length(ctx, path, index)` | Key's byte length excluding its terminator, or 0. |
| `get_string(ctx, path, fallback)` | Borrowed decoded string, or fallback on missing/wrong type. |
| `string_length(ctx, path)` | Decoded byte length excluding its terminator, or 0. |
| `get_number(ctx, path, fallback)` | Borrowed exact number literal, or fallback. |
| `get_int(ctx, path, fallback)` | Int32 for integer literals in range; otherwise fallback. `1.0` and `1e0` use fallback. |
| `get_bool(ctx, path, fallback)` | Boolean value, or fallback. |

Use `type` to distinguish a missing path from JSON null, an empty string or an
empty container. Use string/key lengths when a decoded value contains NUL.
Keys containing NUL can be enumerated but cannot be addressed by the C-string
path API. Duplicate keys are preserved in order; lookup/set/remove target the
last matching key. Removing it reveals an earlier duplicate if present.

## Editing

| Function | Behavior |
| --- | --- |
| `set_json(ctx, path, json)` | Parse and install any JSON fragment at the path. |
| `set_string(ctx, path, text)` | Copy a NUL-terminated UTF-8 string, escaping controls during generation. |
| `set_number(ctx, path, literal)` | Parse a JSON number, retaining its spelling. Other JSON types produce `ERR_TYPE`. |
| `set_int(ctx, path, value)` | Set an int32 value, including `INT32_MIN`. |
| `set_bool(ctx, path, value)` | Set a boolean. |
| `set_null(ctx, path)` | Set JSON null. |
| `remove(ctx, path)` | Remove a value/subtree; `remove(ctx, "")` clears the document. |

Setters replace the root or an existing value, and add missing members to
existing objects. An array's final `-` segment appends; other indexes replace
existing elements. Missing parents are never created implicitly. Removal shifts
subsequent array indexes. Object order is retained on replacement; new members
are appended. `set_json` also permits strings containing NUL via escapes.

All validation, capacity checks and allocations precede linking new nodes.
Any failed edit preserves the old tree. Limits apply to the resulting whole
document, including a fragment's depth relative to its parent. New key text
must also be valid UTF-8.

## Errors

| Code | Meaning |
| --- | --- |
| `DMJSON_OK` (0) | Success. |
| `DMJSON_ERR_INVALID` (-1) | Invalid argument or invalid new UTF-8 key/string. |
| `DMJSON_ERR_MEMORY` (-2) | SAL allocation failed. |
| `DMJSON_ERR_SYNTAX` (-3) | Invalid JSON text. |
| `DMJSON_ERR_NOT_FOUND` (-4) | Missing value/parent or out-of-range array index. |
| `DMJSON_ERR_TYPE` (-5) | Wrong mutation parent/value type. |
| `DMJSON_ERR_BUFFER` (-6) | Output buffer too small. |
| `DMJSON_ERR_FILE` (-7) | Open/read/write failed. |
| `DMJSON_ERR_LIMIT` (-8) | Depth, node count or representable size limit exceeded. |

Inspection APIs return their documented missing/default result for invalid
contexts or paths. Handle checks assume the memory is still live.

## Resources and complexity

`DMJSON_MAX_DEPTH` (64) counts container nesting: a root scalar has depth 0,
a root container depth 1. `DMJSON_MAX_NODES` (4096) counts every value, including
containers. The ceilings reserve no storage and can be changed at compile time.

A node uses an intrusive tree (parent, sibling and child links). No general
list dependency, traversal stack, hash table or recursive call is needed.
Parsing, generation, subtree removal and subtree-size accounting are linear
in the visited data. Pointer lookup scans siblings at each segment. Setters
visit the path, replaced subtree and new fragment; unaffected sibling subtrees
are neither reparsed nor copied. Heap use is proportional to actual values,
key lengths and string/number lengths. Strings are validated twice during
parsing to allocate their exact decoded size. No automatic object exceeds 30
bytes; the largest is the 12-byte int32 conversion buffer. Entire call frames
also contain compiler register saves, spills and outgoing arguments and may
exceed 30 bytes.
