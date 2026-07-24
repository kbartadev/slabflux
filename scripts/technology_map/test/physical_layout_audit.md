# SlabFlux Test: Physical Layout Audit (`physical_layout_test.cpp`)

## 1. Architectural Overview
Because the deterministic core operates on strict hardware boundaries (L1/L2 caches, NUMA controllers), verifying that the C++ compiler honors alignment directives is critical. The `physical_layout_test` acts as a compile-time and runtime validation layer for struct geometry.

## 2. Cache Line Invariants
The audit specifically asserts that the application's definition of `CACHE_LINE_SIZE` exactly matches the target hardware's destructive/constructive interference boundaries (64 bytes).

### Padding Enforcements
It verifies that the `padded_wrapper<T>` successfully isolates small data types (e.g., a 1-byte `char`) into a full 64-byte boundary.
- `alignof` is verified to be 64.
- `sizeof` is verified to be 64.
This ensures False Sharing is mathematically impossible on the hot path.

## 3. NUMA Allocation Sanity
It executes allocation and deallocation routines on `numa_allocator`, guaranteeing that the system correctly interfaces with the `libnuma` bindings to bind memory pages to the local CPU socket.