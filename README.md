# dmjson

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmjson/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmjson/actions/workflows/ci.yml)

dmjson DMOD library module.

## Description

TODO: describe what this module does.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

CMake automatically uses an installed `dmod-dev` SDK. Without an SDK it fetches
dmod's `develop` branch from GitHub. With the SDK installed, a self-contained
module builds without a DMOD checkout or network access.

Pass `-DDMOD_DIR=/path/to/local/dmod` to select a local checkout, even when the
SDK is installed. The checkout needs its submodules: use `git clone --recursive`
or run `git submodule update --init --recursive` in an existing checkout.
To force the GitHub source build while an SDK is installed, pass
`-DDMOD_USE_INSTALLED_SDK=OFF`. Use separate build directories when switching
between source and installed SDK builds.

For an SDK in a custom prefix, pass `-DCMAKE_PREFIX_PATH=/path/to/prefix`.
Existing `DMOD_TOOLS_NAME` profiles also work with the SDK, for example
`-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7` with the corresponding compiler
installed.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

The legacy Make integration uses a source checkout. For a `dmod-dev` installation,
use the CMake commands above.

## Testing

Tests are built automatically alongside the module (see `tests/`). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmjson-local.dmd -y
dmod_loader build/dmf/test_dmjson.dmf
```

## Usage

<TBD>

This library module provides functions that can be used by other modules:

```c
#include "dmjson.h"
```

## API

| Function | Description |
|----------|-------------|
| `dmjson_create()` | Create a new `dmjson_t` instance. |
| `dmjson_destroy()` | Destroy an instance created by `_create()`. |
| `dmjson_is_valid()` | Check whether a handle is a valid instance. |

See [include/dmjson.h](include/dmjson.h) for the full
declarations and [docs/api-reference.md](docs/api-reference.md) for the
complete reference.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmjson`.
## Project Structure

```
dmjson/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmjson.h
├── src/
│   └── dmjson.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmjson_test.c
├── CMakeLists.txt
├── Makefile
├── dmjson.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
