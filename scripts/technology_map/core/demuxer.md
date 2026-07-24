# Blueprint: Compile-Time Tag Demuxing & Vectorized Routing (Deep Dive)

## 1. Architectural Overview
In high-throughput environments (e.g., reading from an `io_uring` CQE ring or an SPSC lock-free conduit), events arrive as raw, untyped memory addresses (`void*`). Identifying the physical C++ structure and routing it to the appropriate execution logic is traditionally a massive bottleneck involving `dynamic_cast`, RTTI, or complex `switch/case` trees.

The `demuxer.hpp` subsystem completely decouples the transport layer from the execution layer using mathematical fold expressions and hardware bit-packing, achieving O(1) branchless routing.

## 2. The `tagged_pointer` Transport Token
Instead of passing naked `void*` pointers or heap-allocated envelopes, SlabFlux relies on the `tagged_pointer`. It exploits the unused upper bits of standard x86-64 memory addresses (which currently only use 48 bits for addressing) to embed structural type information directly into the pointer itself.

```text
=========================================================
   64-BIT TAGGED POINTER LAYOUT (Cache-Line Independent)
=========================================================
[ Bits 63 - 48 ]  [ Bits 47 - 0                             ]
[ 16-Bit Tag ID]  [ 48-Bit Physical Memory Address (void*)  ]
=========================================================
```
* **O(1) Unpacking**: Retrieving the Tag ID and the Address is achieved in a single CPU cycle via bitwise shifting (`ptr >> 48`) and masking (`ptr & 0x0000FFFFFFFFFFFF`), completely avoiding memory dereferencing or cache misses.

## 3. Branchless Dispatch via Fold Expressions
When the `demuxer` receives a `tagged_pointer`, it must resolve the 16-bit ID into a strongly-typed C++ function call on the `pipeline`. 

Instead of generating a massive, branch-prediction-destroying `if-else` block, the Demuxer leverages C++17 Fold Expressions over the variadic `SupportedEvents...` pack.

### 3.1. Synthesizing CMOV (Conditional Move)
```cpp
((current_tag == SupportedEvents::ID ? (
    pipe.dispatch(*reinterpret_cast<SupportedEvents*>(payload)), true
) : false) || ...);
```
Because the `SupportedEvents::ID` values are strictly `static constexpr`, the compiler evaluates this sequence at compile-time. On modern Clang/GCC compilers, this ternary expansion is heavily optimized into:
1. **Jump Tables (Computed Gotos)**: If the IDs are tightly packed.
2. **Conditional Moves (`CMOV`)**: For smaller type packs, utilizing predicative execution that prevents the CPU instruction pipeline from ever flushing due to branch mispredictions.

## 4. Vectorized SIMD Unrolling (`dispatch_simd`)
In extreme ingestion scenarios (like draining a highly active `io_uring` completion queue), processing pointers one by one is sub-optimal. The Demuxer supports raw hardware vectorization using AVX-2 instructions.

### 4.1. The 256-bit SIMD Array
The `dispatch_simd` interface accepts a `__m256i` vector register containing exactly four 64-bit `tagged_pointer` structures loaded simultaneously from main memory.

```cpp
SLAB_FORCE_INLINE void dispatch_simd(__m256i vec) noexcept {
    alignas(32) tagged_pointer tps[4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(tps), vec);
    
    #pragma GCC unroll 4
    for (int i = 0; i < 4; ++i) {
        dispatch(tps[i]);
    }
}
```
* **`_mm256_store_si256`**: Performs a single 256-bit aligned store, moving all 4 transport tokens into L1 cache instantly.
* **`#pragma GCC unroll 4`**: Forces the compiler to physically write out the `dispatch(tps[i])` calls sequentially. This exposes the four consecutive route evaluations to the compiler's instruction scheduler, allowing it to interleave the pipeline logic across available superscalar execution ports.