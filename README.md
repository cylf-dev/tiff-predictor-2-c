# tiff-predictor-2-c

A WebAssembly codec that implements TIFF Horizontal Differencing (Predictor=2) encoding and decoding. Written in C, compiled to Wasm via [Zig](https://ziglang.org/). See [WASM.md](WASM.md) for Wasm-specific details of the codebase (calling convention, memory strategy, exports).

## What it does

TIFF Predictor=2 stores pixel data as row-wise differences rather than absolute values. This codec supports both directions:

- **Encode**: applies horizontal differencing (row-wise differences)
- **Decode**: reverses it via cumulative sum, restoring the original pixel values

Supports 1, 2, and 4 bytes-per-sample data (uint8, uint16, uint32).

## Wasm ABI

The module is built as a WASI reactor and exports four functions:

| Export | Signature | Description |
| ------ | --------- | ----------- |
| `alloc` | `(size: i32) -> ptr` | Allocate memory in the Wasm module |
| `dealloc` | `(ptr, size: i32)` | Free previously allocated memory |
| `decode` | `(input_ptr, input_len: i32, config_ptr, config_len: i32) -> i64` | Decode differenced data |
| `encode` | `(input_ptr, input_len: i32, config_ptr, config_len: i32) -> i64` | Apply horizontal differencing |

In addition to the input data, `encode` and `decode` expect a JSON config with `bytes_per_sample` and `width` keys, e.g. `{"bytes_per_sample": 2, "width": 256}`. They return the result as `(out_ptr << 32) | out_len`, [packing the output pointer and length into a single i64](WASM.md#packing-the-return-value).

## Prerequisites

- [Zig](https://ziglang.org/download/) 0.15.x (used both as the C cross-compiler and build system)

No other dependencies are required. Zig is used purely as a build tool here — no Zig code is involved. Zig bundles a C/C++ cross-compiler (based on Clang) and pre-built libc implementations for many targets, including `wasm32-wasi`. This means `zig cc --target=wasm32-wasi` can compile C to Wasm with libc support out of the box, with no separate toolchain installation required.

## Build

```sh
zig build                          # debug build
zig build -Doptimize=ReleaseSmall  # optimized for size (recommended)
```

Output: `zig-out/tiff-predictor-2-c.wasm`

## Development

```sh
zig build test
```

Tests compile the C source natively (not to Wasm) and exercise the config parser, cumulative-sum, differencing, and roundtrip logic directly.

This project uses [pre-commit](https://pre-commit.com/) to run checks before each commit. Install pre-commit and then enable the hooks:

```sh
brew install pre-commit  # or: pipx install pre-commit
pre-commit install
```

To run all checks manually:

```sh
pre-commit run --all-files
```
