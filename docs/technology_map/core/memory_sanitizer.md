# SlabFlux Core: Memory Sanitizer (`memory_sanitizer.hpp`)

## 1. Architectural Overview
Standard `std::memset` heavily relies on underlying `glibc` implementations, which feature unpredictable branch trees (to handle varying block sizes) and OS-level optimizations that can introduce jitter.
The `memory_sanitizer` provides a deterministic, hardware-accelerated alternative for wiping memory slabs and HugePages without branching.

## 2. AVX-512 Stream Wiping
When an object is returned to a `mpmc_pool`, leaving dirty residual data violates cryptographic integrity.
- The sanitizer utilizes `_mm512_setzero_si512()` and unrolled `_mm512_storeu_si512()` instructions.
- It zeroes out exact multiples of 64 bytes in a flat execution path.
- Because SlabFlux strictly aligns all structures to `CACHE_LINE_SIZE`, the sanitizer never encounters misaligned tails, eliminating the need for scalar fallback loops.

## 3. Quarantine Bounding
If a `fault_record` dictates that a memory region has been poisoned (e.g., due to an `mce_listener` alert), the sanitizer executes a "Quarantine Wipe". 
It aggressively fills the cache lines with specific hardware-trap sentinels (`0xDEADBEEF`), ensuring that if a dangling pointer ever accesses the dead block, it immediately segfaults predictably rather than injecting silent mathematical errors into the causal mesh.