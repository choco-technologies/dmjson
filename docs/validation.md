# Validation

Validated on 2026-11-04 using GCC 13.3.1 for ARM Cortex-M7 and the local DMOD
source checkout used by dmboot. Commands below assume a normal configured SDK
or a source checkout selected through `DMOD_DIR`.

## Results

| Check | Result |
| --- | --- |
| Host `.dmf` library/test build, real `dmod_loader` execution | 13/13 steps passed |
| Offline host build, `-Wall -Wextra -Werror -Wpedantic`, ASan + UBSan | Passed |
| Allocation failure at every allocation in parse and all setter families | Passed, old document retained, no leaked blocks/bytes |
| File short reads, write failures, missing paths, 1024-byte buffered file round trip | Passed |
| Depth and whole-document node ceilings, including mutation | Passed |
| Deterministic arbitrary-byte inputs and truncations | 50,000 fuzz inputs passed without sanitizer errors |
| Random structured JSON / malformed text vs Python `json` | 5,055 cases passed |
| Cortex-M7 cross-build of library and tests | Passed |
| STM32F746G-DISCO, existing dmboot, UART 921600 | 13/13 steps passed, including SAL file round trip |

The same functional test source runs through the native harness, DMOD loader
and hardware. Additional native-only tests inject allocator and I/O failures.
Fuzzing and differential checks use fixed seeds for reproducibility.

## ARM footprint

With the default DMOD Cortex-M7 profile (`-O2`) and this toolchain:

| Item | Bytes |
| --- | ---: |
| `dmjson.dmf` file | 8278 |
| ELF text | 7728 |
| ELF data (including DMOD API metadata) | 532 |
| ELF BSS | 0 |
| One tree node | 32 |
| Document context | 16 |
| Temporary parser state | 36 |
| Temporary writer state | 28 |
| Buffered file output chunk, allocated only for file generation | 128 |
| Largest automatic object in library source (`set_int` buffer) | 12 |

Node/key/text allocations additionally incur the platform allocator's metadata
and alignment costs. Context creation allocates only the context. Parsing adds
one node per value plus exact-length key and string/number storage; null and
boolean values allocate no text. A failed transactional replacement needs the
old tree and candidate tree simultaneously until it can commit or unwind.

GCC's object-size diagnostic found no objects above 30 bytes other than ROM
string literals used by DMOD API registration. `-fstack-usage` reports a maximum
individual library function frame of 56 bytes in this build, including compiler
register saves/spills and call arguments. The 30-byte rule applies to individual
automatic objects; an entire ABI call frame is larger. There are no recursive
parser, generator or destruction calls, and no depth-sized stack arrays.

## Hardware procedure

Built using `-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7`. Transferred `dmjson.dmf`
and `test_dmjson.dmf` with dmboot's `scripts/ymodem_send.py` into
`/tmp/dmjson-validation` on STM32's RAM filesystem. Added that directory to the
runtime module search path and invalidated the loader's search-path cache using
a temporary RAM tool calling `Dmod_InvalidateModuleSearchPaths()`. Ran
`test_dmjson` through the existing dmboot shell:

```text
=== Results: 13/13 passed ===
```

Afterwards disabled/unloaded `dmjson`, restored the previous search path and
cache, and removed all uploaded binaries. The file round-trip test removes its
own temporary JSON file. Firmware was not reflashed and the ESP device was not
accessed.

## Reproduce on a host

```bash
cmake -S . -B build/native -DDMJSON_NATIVE_TESTS_ONLY=ON
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure

cmake -S . -B build/host
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

CI runs these host suites and a Cortex-M7 module cross-build. Hardware validation
is manual; no connected board is required by CI.
