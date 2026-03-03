# WebAssembly in this project

This document explains the Wasm-specific aspects of this codec's codebase. For general Wasm concepts (what Wasm and WASI are, linear memory, memory growth, etc.), see the knowledge base in the chonkle repo at `docs/wasm/`.

## How the host interacts with the module

Wasm modules have their own linear memory — a flat byte array that the host can read and write. The workflow for calling `encode` or `decode` looks like this:

```text
Host                                    Wasm module
────                                    ───────────
1. Call alloc(input_size)          →    malloc memory, return pointer
   ← pointer into Wasm memory

2. Write input bytes into Wasm
   memory at that pointer

3. Call alloc(config_size)         →    malloc memory, return pointer
   ← pointer into Wasm memory

4. Write JSON config bytes into
   Wasm memory at that pointer

5. Call encode(input_ptr,          →    Read input + config from own memory,
         input_len,                     process data,
         config_ptr,                    malloc output buffer,
         config_len)                    return (out_ptr << 32) | out_len
   ← packed i64 result

6. Unpack result to get out_ptr
   and out_len

7. Read out_len bytes from Wasm
   memory at out_ptr

8. Call dealloc(input_ptr, input_size)  →  free
   Call dealloc(config_ptr, config_size) → free
   Call dealloc(out_ptr, out_len)        → free
```

All pointers in this flow are offsets into the Wasm module's own linear memory, not host memory addresses. The host runtime provides functions to read/write the Wasm memory at these offsets.

## Memory allocation strategy

This codec knows the output size upfront (it equals `input_len`), so it allocates output buffers at their final size in a single `malloc` call. This avoids `realloc` copies and incremental `memory.grow` calls. For background on how Wasm linear memory growth works and strategies for codecs that don't know their output size upfront, see the chonkle knowledge base (`docs/wasm/MEMORY.md`).

## Packing the return value

Wasm functions can only return a single scalar value. `encode` and `decode` each need to return both a pointer to the output data and the length of that data. It packs them into one `i64`:

```text
upper 32 bits: output pointer    (fits because wasm32 pointers are 32 bits)
lower 32 bits: output length
```

The host unpacks with:

```text
ptr = result >> 32
len = result & 0xFFFFFFFF
```

## The export_name attribute

```c
__attribute__((export_name("encode")))
int64_t encode(...) { ... }

__attribute__((export_name("decode")))
int64_t decode(...) { ... }
```

By default, a C function compiled to Wasm is not visible to the host — it's internal to the module. The `export_name` attribute tells the compiler to add the function to the module's export table so the host can call it by name. The build also uses `-fvisibility=hidden` to ensure that only explicitly exported functions are visible.
