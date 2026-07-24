# SlabFlux Core: Physical Layout (`physical_layout.hpp`)
# Physical Layout (`physical_layout.hpp`)

## 1. Architectural Overview
The `physical_layout` module provides the foundational compile-time invariants that govern the memory geometry of all hot-path data structures. It enforces strict alignment and padding rules to guarantee that C++ structs map perfectly to the underlying CPU cache architecture, eliminating False Sharing and cache-line splitting.

## 2. Core Components

### `CACHE_LINE_SIZE`
A `static constexpr` constant that resolves to `std::hardware_constructive_interference_size` (typically 64 bytes on x86-64). This serves as the fundamental quantum for all memory alignment operations within the framework.

### `padded_wrapper<T>`
A transparent struct wrapper that forces any enclosed type `T` to occupy exactly one full cache line.
- **Alignment**: Uses `alignas(CACHE_LINE_SIZE)` to ensure the struct's base address starts on a 64-byte boundary.
- **Padding**: If `sizeof(T)` is less than 64 bytes, the wrapper automatically adds padding bytes to fill the remainder of the cache line.

This is critical for isolating atomic flags or cursors (like those in `spsc_conduit`) from adjacent, mutable data, thereby preventing MESI cache-line invalidation storms.

### `numa_allocator`
A low-level memory allocator that interfaces directly with the NUMA (Non-Uniform Memory Access) control APIs of the operating system (`numa_alloc_onnode`, `numa_free`).
- It allows memory slabs (like those for `mpmc_pool`) to be physically allocated on the specific memory controller attached to the CPU socket where the thread is pinned.
- This eradicates cross-socket memory access, which is a primary source of non-deterministic latency in multi-CPU server environments.