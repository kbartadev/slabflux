# SlabFlux Core: Eternal Memory (`eternal_memory.hpp`)

## 1. Architectural Overview
In long-running deterministic systems, certain global state matrices (like static routing tables, configuration maps, or instrument directories) are initialized once at boot and read continuously for the lifetime of the application. The `eternal_memory` module provides an ultra-lean allocator specifically for this "immortal" data.

## 2. Zero-Overhead Allocation
Standard memory pools maintain complex free-lists, epoch counters, or atomic cursors to manage the lifecycle of objects. 
The `eternal_memory` pool structurally abandons deallocation entirely:
- **Bump-Pointer Geometry**: It allocates memory by simply advancing an integer offset across a pinned `MAP_HUGETLB` memory slab.
- **No Free() Support**: Because the objects are guaranteed to exist until the process terminates, there is zero metadata stored per-object, ensuring 100% cache-line purity for the payload itself.

## 3. Hardware Alignment
The allocator natively supports `alignas` requirements for AVX-512 integration. When a block of memory is requested, the bump pointer automatically rounds up to the nearest 64-byte boundary, guaranteeing that eternal structures never suffer from False Sharing or misaligned SIMD loads during the hot path.

## 4. Ignition Phase Sealing
Once the `ignition_manifest` has finished booting the node and all static data is loaded, the `eternal_memory` slab can optionally execute an `mprotect(PROT_READ)` syscall. This physically write-protects the entire memory range at the hardware page-table level, permanently shielding the static state from stray pointer corruptions.