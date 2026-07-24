# Foundation: Runtime Domain (`slabflux/core/runtime_domain.hpp`)

## 1. Architectural Justification
In ultra-low-latency environments, allocating heterogenous event types from a single, generic memory pool leads to severe memory fragmentation and unpredictable cache-line geometry. The `runtime_domain` serves as a strictly typed, O(1) logical container that pre-allocates and manages dedicated memory pools for specific domain events.

## 2. Hardware Implementation Directives
- **Type-Segregated Memory Slabs**: Instantiates a dedicated `spsc_pool` or `mpmc_pool` for each registered event type. Iterating through a homogeneous array guarantees near-perfect L1/L2 data cache hit rates.
- **Zero Fragmentation**: Slabs are homogeneously sized based on `sizeof(T)`, eliminating the metadata overhead (block headers) inherent in dynamic allocators like `dlmalloc` or `jemalloc`.
- **Compile-Time Sovereignty**: Employs variadic template parameter packs (`Events...`). If a developer attempts to route an unregistered event, a `static_assert` blocks compilation, preventing rogue heap allocations on the hot path.

## 3. Bibliography & Proofs
1. **Bonwick, J.** (1994). *The Slab Allocator: An Object-Caching Kernel Memory Allocator*. USENIX Summer. (Fragmentation proofs and object caching).
2. **Levinthal, S.** (2009). *Performance Paradoxes in Multicore Architectures*. (Impact of data geometry and L1 cache hits on throughput).
3. **Berger, E. D., et al.** (2000). *Hoard: A Scalable Memory Allocator for Multithreaded Applications*. ASPLOS.