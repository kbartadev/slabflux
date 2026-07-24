# SlabFlux Core: Smart String (`smart_string.hpp`)

## 1. Architectural Overview
Standard library strings (`std::string`) are strictly banned on the SlabFlux hot path due to their reliance on dynamic heap allocation (`malloc`/`new`), which invokes non-deterministic OS locks. 
The `smart_string` is a zero-allocation, fragmented text container designed to handle variable-length network data (like JSON or FIX protocol messages) entirely within lock-free memory pools.

## 2. Fragmented Chunk Architecture
Instead of contiguous heap arrays, `smart_string` operates as a linked list of strictly sized fragments.

### The `string_chunk` Node
Each piece of text is stored in a `string_chunk` structure (e.g., 64 bytes or 256 bytes). 
If a string exceeds the capacity of a single chunk, the `smart_string` automatically requests an additional chunk from the `global_string_pool` and seamlessly links it via an internal `next` pointer.

### Wait-Free Allocation
The `global_string_pool` is built on top of the `mpmc_pool` (or localized `spsc_pool` variants). 
- When a `smart_string` needs to expand, it executes a wait-free pop from the pre-allocated chunk pool. 
- When the `smart_string` goes out of scope, its RAII destructor instantly releases all linked chunks back to the pool in an O(1) vectorized burst (`release_batch`).

## 3. C++ API Integration & Ergnomics
Despite its complex fragmented backend, the `smart_string` exposes a standard, highly ergonomic interface:
- **Iterators**: Provides transparent forward iterators that seamlessly jump across chunk boundaries, allowing seamless integration with algorithms like `std::find` or SIMD parsers.
- **String Views**: Capable of zero-copy projection into `std::string_view` when the data size remains confined within a single contiguous chunk.
- **Appends**: Fast `operator+=` implementations that manage the chunk-linking math automatically.

## 4. SIMD Parsing Optimization
Because every `string_chunk` guarantees a fixed, known capacity padded to cache-line boundaries (e.g., `alignas(64)`), it is perfectly tailored for AVX-2 and AVX-512 text parsing (`io::header_parser`). The CPU can load an entire chunk into a ZMM register without risking segmentation faults from over-reading typical null-terminated heap strings.