# SlabFlux Core: Slab Allocator (`slab_allocator.hpp`)

## 1. Architectural Overview
While `pinned_allocator` focuses on NUMA-aware physical RAM pinning, the `slab_allocator` provides a traditional, highly predictable contiguous block manager. It is designed to service zero-allocation frameworks by recycling memory slots for homogeneous structures (like order objects or network frame wrappers) without ever yielding to the OS.

## 2. Deterministic Fragmentation Control
Standard `malloc` implementations fragment memory when objects of various sizes are allocated and freed randomly. 
The `slab_allocator` strictly allocates typed arrays (`T[N]`) upfront.
- **Object Recycling**: When an object is "freed," it is not returned to the kernel. Instead, its pointer is pushed onto an internal, cache-aligned LIFO free-list.
- **Spatial Predictability**: Because all objects of type `T` reside in the exact same contiguous virtual memory region, the CPU's hardware prefetcher can effortlessly predict access patterns during iterative sweeps, minimizing L1 Data Cache misses.

## 3. Wait-Free Ingress/Egress
The free-list operates entirely without mutexes or blocking synchronization:
- By using bounded array indices instead of dynamic node allocations for the free-list itself, pushing and popping available slots compiles down to a few arithmetic instructions.
- For single-threaded data planes (`spsc` deployments), the allocator skips atomic Compare-And-Swap (CAS) instructions entirely, resolving allocations in under 5 CPU cycles.

## 4. Lifecycle Integration
The `slab_allocator` pairs seamlessly with `managed_data<T>`. When the RAII wrapper expires, it invokes the allocator's `release(ptr)` method directly, guaranteeing leak-free operation in ultra-high-frequency environments where millions of objects are instantiated and destroyed per second.