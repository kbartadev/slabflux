# Foundation: AVX-512 Search Backend (`slabflux/compute/avx512_search_backend.hpp`)

## 1. Architectural Justification
Finding a specific order or instrument in a state matrix traditionally relies on Hash Maps or Binary Trees, both of which involve unpredictable memory chasing (pointer chasing) that destroys performance. The `avx512_search_backend` uses dense, parallelized silicon logic for memory lookups.

## 2. Hardware Implementation Directives
- **8-Way Parallel Lookup**: Instead of scalar iteration, the backend packs 64-bit keys (e.g., Instrument IDs) into contiguous `alignas(64)` arrays. It uses `_mm512_cmp_epi64_mask` to compare 8 keys simultaneously in a single CPU cycle.
- **Bit-Scan Forward (BSF)**: The resulting 8-bit mask is passed to the `__builtin_ctz` (Trailing Zeros) intrinsic. If a match exists, the exact array index is yielded instantly.
- **Branchless Miss Handling**: If the key is absent, the zero-mask structurally bypasses the state mutation logic via conditional moves (`CMOV`), completely avoiding branch-prediction penalties.

## 3. Bibliography & Proofs
1. **Intel Corporation**. *Intel Architecture Instruction Set Extensions Programming Reference*. (AVX-512 mask registers and comparison instructions).
2. **Lemire, D.** (2015). *Vectorized Dictionary lookups*. (SIMD-accelerated array probing).
3. **Fog, Agner**. (2021). *Optimizing subroutines in assembly language*.