# SlabFlux Core: Cache Shield (`cache_shield.hpp`, `hot_path_alignment.hpp`)

## 1. Architectural Overview
At nanosecond latencies, the physical geometry of C++ structures in memory is more critical than the algorithms operating on them. The `cache_shield` and associated alignment headers enforce strict spatial rules across the entire codebase to protect L1/L2 cache coherency.

## 2. False Sharing Eradication
False Sharing occurs when two independent threads mutate different variables that happen to reside on the same 64-byte hardware cache line, forcing the CPUs to continuously invalidate and re-fetch the data across the QPI/Infinity Fabric.

### `alignas` Enforcement
The `hot_path_alignment.hpp` header leverages C++20 `std::hardware_constructive_interference_size` (which resolves to 64 bytes on x86-64).
- All critical synchronization primitives (such as the `head` and `tail` cursors in `spsc_conduit`) are explicitly wrapped in `alignas(64)` padding blocks.
- The `cache_shield` provides structural template wrappers (`padded_wrapper<T>`) that automatically pad any user-defined domain event to exactly fill or perfectly tile across cache lines.

## 3. Spatial Locality Optimization
For data that *is* read sequentially (like the internal arrays of the `branchless_engine`), the `cache_shield` ensures dense packing without internal gaps.
- It utilizes `static_assert` chains during compilation to verify that `sizeof(T)` is a multiple of the SIMD vector width (e.g., 32 or 64 bytes).
- If a developer adds a `bool` flag to a struct that breaks the 64-byte alignment, the build instantly fails with an invariant violation, preventing degraded layout geometries from ever reaching production.

## 4. Forced Cache Flushing (`buffer_flush`)
When crossing persistence boundaries (like appending to the `durable_journal`), the `cache_shield` exposes explicit instructions to manage cache eviction safely.
- `flush_cache_line(ptr)` executes the `CLWB` (Cache Line Write Back) or `CLFLUSHOPT` hardware intrinsics.
- This asynchronously flushes the specific cache line to main memory/NVMe without forcefully evicting it from the L1 cache, maintaining peak performance for subsequent reads.