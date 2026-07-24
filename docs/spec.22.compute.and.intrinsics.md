# Compute Layer & Hardware Intrinsics

The `slabflux::compute` namespace acts as the direct conduit between high-level C++ logic and raw silicon instructions. It guarantees the compiler never attempts to auto-generate suboptimal instruction streams.

## `slabflux::compute::intrinsics`
A comprehensive, aggressively optimized wrapper encompassing architecture-specific hardware instructions (AVX2, AVX-512, BMI1, BMI2). It delivers a unified, compiler-agnostic API for the RTE.
* **Bit Manipulation (BMI):** Exposes instantaneous O(1) execution for population counts (`_popcnt64`), trailing zeros (`_tzcnt_u64`), and advanced bit-field extractions. These are indispensable for executing ultra-fast hash table lookups and complex bitmask evaluations.
* **Hardware Prefetching:** Grants absolute, manual control over CPU cache lines via `_mm_prefetch`. The pipeline utilizes `_MM_HINT_T0` to proactively stream upcoming event payloads into the L1 cache milliseconds before the logic handler is even invoked, perfectly masking memory latency.
* **Non-Temporal Stores:** During high-throughput network transmission, outgoing byte frames are blasted directly to the memory controller using non-temporal streaming instructions (`_mm256_stream_si256`). This deliberately bypasses the CPU cache hierarchy, preventing network payloads from polluting the L1/L2 caches containing critical trading algorithms.
