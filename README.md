# tiff-predictor-2-c

A WebAssembly codec that implements TIFF Horizontal Differencing (Predictor=2) encoding and decoding. Written in C, compiled to Wasm via [Zig](https://ziglang.org/). See [WASM.md](WASM.md) for Wasm-specific details of the codebase (calling convention, memory strategy, exports).

## What it does

TIFF Predictor=2 stores pixel data as row-wise differences rather than absolute values. This codec supports both directions:

- **Encode**: applies horizontal differencing (row-wise differences)
- **Decode**: reverses it via cumulative sum, restoring the original pixel values

Supports 1, 2, and 4 bytes-per-sample data (uint8, uint16, uint32).

## Wasm ABI

The module is built as a WASI reactor and implements the chonkle core ABI with the binary port-map wire format. It exports:

| Export | Signature | Description |
| ------ | --------- | ----------- |
| `memory` | Memory | Linear memory (provided by the toolchain) |
| `alloc` | `(size: i32) -> i32` | Allocate memory in the Wasm module |
| `dealloc` | `(ptr: i32, size: i32)` | Free previously allocated memory |
| `decode` | `(port_map_ptr: i32, port_map_len: i32) -> i64` | Decode differenced data |
| `encode` | `(port_map_ptr: i32, port_map_len: i32) -> i64` | Apply horizontal differencing |

`encode` and `decode` receive a single serialized port-map containing all input ports (`bytes`, `bytes_per_sample`, `width`). They return a serialized port-map with a single `bytes` output port, packed as `(out_ptr << 32) | out_len`. See [WASM.md](WASM.md) for details on the calling convention and port-map wire format.

## Prerequisites

- [Zig](https://ziglang.org/download/) 0.15.x (used both as the C cross-compiler and build system)

No other dependencies are required. Zig is used purely as a build tool here — no Zig code is involved. Zig bundles a C/C++ cross-compiler (based on Clang) and pre-built libc implementations for many targets, including `wasm32-wasi`. This means `zig cc --target=wasm32-wasi` can compile C to Wasm with libc support out of the box, with no separate toolchain installation required.

## Build

```sh
zig build                          # debug build
zig build -Doptimize=ReleaseSmall  # optimized for size (recommended)
```

Output: `zig-out/tiff-predictor-2-c.wasm`

## Codec signature

The `.wasm` binary includes an embedded `chonkle:signature` custom section declaring the codec's identifier, implementation name, and input/output ports. The signature is defined in [`signature.json`](signature.json) and embedded automatically by the release workflow.

To embed locally after building:

```sh
python3 embed_signature.py zig-out/tiff-predictor-2-c.wasm signature.json
```

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
