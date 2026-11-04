# dmjson

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmjson/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmjson/actions/workflows/ci.yml)

JSON parsing, editing and generation for embedded DMOD applications, following
`dmini`'s opaque-context and SAL allocation/file API pattern.

Supports objects, arrays, strings, booleans, null and exact JSON number literals.
The parser validates UTF-8, escape sequences, surrogate pairs and JSON grammar.
JSON Pointer paths address nested data without allocating temporary lookup keys.
All parsing, generation and destruction use iterative tree walks.

## Usage

```c
#include "dmod.h"
#include "dmjson.h"

int configure(void)
{
    dmjson_t json = dmjson_create();
    if (!json) return DMJSON_ERR_MEMORY;
    int result = dmjson_parse_string(json,
        "{\"network\":{\"port\":8080},\"enabled\":true}");
    if (result == DMJSON_OK) {
        int32_t port = dmjson_get_int(json, "/network/port", 80);
        Dmod_Printf("Port: %ld\n", (long)port);
        result = dmjson_set_int(json, "/network/port", 9000);
    }
    if (result == DMJSON_OK) {
        int size = dmjson_generate_string(json, NULL, 0);
        if (size < 0) result = size;
        else {
            char *text = Dmod_Malloc((size_t)size);
            if (!text) result = DMJSON_ERR_MEMORY;
            else {
                result = dmjson_generate_string(json, text, (size_t)size);
                if (result > 0) { Dmod_Printf("%s\n", text); result = DMJSON_OK; }
                Dmod_Free(text);
            }
        }
    }
    dmjson_destroy(json);
    return result;
}
```

Link a consuming module with `dmod_link_modules(your_module dmjson@0.1)`.
Use `""` for the document root, `/items/0` for an array element, and
`/items/-` to append through a setter. Escape `~` as `~0` and `/` as `~1`
in object keys. Setters require the parent to exist; `dmjson_set_json` can
create entire nested objects or arrays at once.

## Embedded resource use

- No recursion or variable-length arrays. The largest automatic buffer is
  12 bytes; contexts, nodes and I/O buffers live on the SAL heap.
- A node occupies 32 bytes on 32-bit ARM, plus exact-size allocations for
  its key and string/number text (including terminators). Containers share
  storage with scalar payloads. Lookups allocate nothing.
- Parsing and generation are linear in input/output size. Array indexing and
  object lookup scan siblings; there is no hash table or floating-point dependency.
- Numbers retain their spelling and precision. `get_int` checks int32 range
  without overflow; `get_number` returns the literal for application conversion.
- Default limits are 64 container levels and 4096 nodes, enforced on parsing
  and editing. These are ceilings, not preallocated capacities.
- Failed parses/edits preserve the old document. This requires old and new
  subtrees to coexist temporarily. File parsing also holds the source file in
  a heap buffer; file generation uses a 128-byte heap buffer.

See [the API reference](docs/api-reference.md) for ownership, errors, complexity
and limitations, and [validation notes](docs/validation.md) for measured results.

## Building

```bash
cmake -S . -B build/host
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

CMake uses an installed `dmod-dev` SDK when available, otherwise fetches DMOD
`develop`. Select a local checkout with `-DDMOD_DIR=/path/to/dmod`, a custom SDK
prefix with `-DCMAKE_PREFIX_PATH=/path/to/sdk`, or force fetching with
`-DDMOD_USE_INSTALLED_SDK=OFF`. Source checkouts need initialized submodules.
The Make integration also supports `make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod`.

For STM32F746G-DISCO modules:

```bash
cmake -S . -B build/arm -DDMOD_TOOLS_NAME=arch/armv7/cortex-m7
cmake --build build/arm --parallel
```

Resource limits can be overridden when compiling the library, for example
`-DCMAKE_C_FLAGS="-DDMJSON_MAX_DEPTH=16 -DDMJSON_MAX_NODES=256"`. Applications
must use the same definitions if referring to the limit macros in the header.

## Testing

The standard build produces `dmjson.dmf` and `test_dmjson.dmf`. CTest uses
`dmf-get` to install local dependencies and runs tests through `dmod_loader`.
The same test module runs on STM32 through dmboot. The file round-trip test
uses `/tmp/dmjson-roundtrip-test.json`; override `DMJSON_TEST_FILE_PATH` in
test compiler flags if another writable directory is needed.

The offline host harness instruments the implementation with ASan/UBSan,
checks allocation failures/leaks and short file I/O, fuzzes 50,000 byte inputs,
and compares over 5,000 cases against Python's JSON parser:

```bash
cmake -S . -B build/native -DDMJSON_NATIVE_TESTS_ONLY=ON
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

This harness supplies only a test SAL shim; production uses real DMOD dynamic
APIs. Python 3 enables the differential suite. GCC/Clang sanitizers can be disabled
with `-DDMJSON_SANITIZERS=OFF` if unavailable.

## Project structure

```text
dmjson/
├── include/dmjson.h       Public API
├── src/dmjson.c           Parser, tree editing and generation
├── tests/dmjson_test.c    Tests shared by loader, hardware and native harness
├── tests/native/          SAL shim, sanitizers, fault injection and differential tests
├── docs/                  API reference and validation notes
├── CMakeLists.txt
├── Makefile
├── dmjson.dmr
└── manifest.dmm
```

Author: Patryk Kubiak. Licensed under [MIT](LICENSE).
