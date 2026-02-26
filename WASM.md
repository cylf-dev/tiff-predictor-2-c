# WebAssembly in this project

This document explains why this project compiles to WebAssembly and what the WASM-specific terms in the codebase mean.

## Why WebAssembly?

This codec is designed to run inside a host application (like a Python program) that needs to encode or decode TIFF image data. Rather than writing this logic in the host language, we write it in C and compile it to WebAssembly. The host loads the `.wasm` file and calls its exported functions through a WASM runtime.

This gives us:

- **Portability** — the same `.wasm` binary runs on any platform that has a WASM runtime (Linux, macOS, Windows, browser, etc.), without needing to compile separate native binaries for each OS and architecture.
- **Sandboxing** — WASM modules run in an isolated memory space. They cannot access the host's filesystem, network, or memory unless the host explicitly allows it. A bug in the codec can't corrupt the host process.
- **Performance** — C compiled to WASM runs close to native speed, much faster than equivalent pure-Python code.

## What is WASI?

**WASI** (WebAssembly System Interface) is a standard set of APIs that give WASM modules access to OS-like functionality — things like memory allocation, file I/O, and clocks. Think of it as a minimal, portable "operating system" interface for WASM.

Our codec uses WASI because the C code calls `malloc` and `free`. These libc functions need an underlying memory allocator, which WASI provides. Without WASI, we'd have to write our own allocator from scratch.

The build target is `wasm32-wasi`:

- `wasm32` — 32-bit WebAssembly (pointers are 32 bits)
- `wasi` — use the WASI system interface (as opposed to a bare `wasm32-unknown` target with no system interface at all)

## What is a WASI reactor?

WASM modules come in two execution models:

- **Command** — has a `_start` function (like `main()` in C). The runtime calls `_start`, the module runs to completion, and then it's done. This is the default and is analogous to running a CLI program.
- **Reactor** — has no `_start`. Instead it exports individual functions that the host calls on demand, potentially many times over the module's lifetime. The module stays alive between calls.

This codec is built as a **reactor** (via `-mexec-model=reactor` in the build flags) because the host needs to call `alloc`, `encode` or `decode`, and `dealloc` as separate steps. The module must stay alive between these calls so that memory allocated by `alloc` is still valid when `encode` or `decode` reads it.

## How the host interacts with the module

WASM modules have their own linear memory — a flat byte array that the host can read and write. The workflow for calling `encode` or `decode` looks like this:

```text
Host                                    WASM module
────                                    ───────────
1. Call alloc(input_size)          →    malloc memory, return pointer
   ← pointer into WASM memory

2. Write input bytes into WASM
   memory at that pointer

3. Call alloc(config_size)         →    malloc memory, return pointer
   ← pointer into WASM memory

4. Write JSON config bytes into
   WASM memory at that pointer

5. Call encode(input_ptr,          →    Read input + config from own memory,
         input_len,                     process data,
         config_ptr,                    malloc output buffer,
         config_len)                    return (out_ptr << 32) | out_len
   ← packed i64 result

6. Unpack result to get out_ptr
   and out_len

7. Read out_len bytes from WASM
   memory at out_ptr

8. Call dealloc(input_ptr, ...)    →    free
   Call dealloc(config_ptr, ...)   →    free
   Call dealloc(out_ptr, ...)      →    free
```

All pointers in this flow are offsets into the WASM module's own linear memory, not host memory addresses. The host runtime provides functions to read/write the WASM memory at these offsets.

## Memory growth

WASM linear memory starts at a fixed initial size (typically a few 64 KiB pages). When the module needs more memory — for example, when `malloc` can't satisfy a request from the existing heap — the memory must grow.

### How it works

The WASI libc bundled into the `.wasm` binary includes a `dlmalloc` allocator. It manages a freelist within linear memory and handles growth automatically:

1. C code calls `malloc(size)`.
2. `dlmalloc` checks its freelist. If it can satisfy the request from already-available space, it does.
3. If not, `dlmalloc` calls `memory.grow(n_pages)` — a WebAssembly instruction that appends `n` 64 KiB pages to the end of linear memory.
4. The WASM runtime (wasmtime, wasmer, etc.) fulfills the growth. If the module declares a maximum memory size and the request exceeds it, `memory.grow` returns -1 and `malloc` returns `NULL`.

Because our module is built as a WASI reactor with libc, all of this is handled transparently — the C code just calls `malloc`/`free` and never deals with `memory.grow` directly. Without WASI libc (a bare `wasm32-unknown` target), you would need to call `__builtin_wasm_memory_grow(0, n_pages)` yourself or have the host grow the memory from outside.

### Growth is contiguous and append-only

`memory.grow` always appends pages to the end of linear memory. The entire address space remains one flat, contiguous byte array. Existing addresses and pointers are never invalidated by growth — new pages simply appear at higher addresses. This is simpler than native virtual memory, where `mmap` might return non-contiguous regions.

However, linear memory **never shrinks** during the lifetime of an instance. `free` returns memory to `dlmalloc`'s freelist for reuse, but no pages are released back to the host. This means a module's memory high-water mark stays elevated after processing a large input. Long-lived instances can accumulate memory over time, which is one reason some designs spin up fresh instances per task.

### Performance considerations

- **`memory.grow` is not free** — the runtime may need to zero-initialize new pages or update internal bookkeeping. Growing one page at a time in a loop is slower than growing many pages at once. `dlmalloc` handles this reasonably by requesting pages in batches.
- **`realloc` may copy** — if `dlmalloc` can't extend an allocation in place, `realloc` allocates a new block, copies the data, and frees the old one. For large buffers this is expensive and temporarily uses 2x the memory. This codec avoids the issue by allocating output buffers at their final size upfront (the output size equals `input_len`).
- **Preallocation helps** — when the output size is known, allocating once at the correct size (as this codec does) avoids both realloc copies and incremental `memory.grow` calls.

### Strategies for dynamically growing output buffers

This codec knows the output size upfront (`input_len`), so it allocates once. But some codecs — particularly compression or variable-length encoding — don't know the output size in advance. In those cases, the buffer must grow dynamically during processing. Common strategies:

- **Geometric growth** — start with a reasonable initial size and double the buffer (via `realloc`) each time it fills up. This amortizes the cost of copies to O(1) per element over time, at the expense of up to 2x memory overallocation. This is the standard approach used by dynamic arrays (e.g., C++ `std::vector`). Each `realloc` that can't extend in place will copy the entire buffer to a new location, so the tradeoff is fewer, larger copies versus many small allocations.
- **Worst-case preallocation** — if you can bound the maximum output size (e.g., LZ4 defines a maximum expansion ratio), allocate that size upfront and truncate or `realloc` down at the end. This avoids all intermediate copies but may waste memory if the bound is loose.

Since the host reads output as a contiguous region at a single pointer+length, the output must ultimately be a single contiguous buffer in linear memory. Geometric growth is the typical choice when the final size isn't known upfront.

## Packing the return value

WASM functions can only return a single scalar value. `encode` and `decode` each need to return both a pointer to the output data and the length of that data. It packs them into one `i64`:

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

By default, a C function compiled to WASM is not visible to the host — it's internal to the module. The `export_name` attribute tells the compiler to add the function to the module's export table so the host can call it by name. The build also uses `-fvisibility=hidden` to ensure that only explicitly exported functions are visible.
