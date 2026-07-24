# SlabFlux Core: Runtime Domain (`runtime_domain.hpp`)

## 1. Architectural Overview
In ultra-low-latency environments, allocating heterogenous event types from a single, generic memory pool leads to severe memory fragmentation and unpredictable cache-line geometry. The `runtime_domain` serves as a strictly typed, O(1) logical container that pre-allocates and manages dedicated memory pools for specific domain events.

## 2. Type-Segregated Memory Slabs
The domain operates by instantiating a dedicated `spsc_pool` or `mpmc_pool` for each event type registered in its variadic template parameter pack (`Events...`).

- **Spatial Locality**: Because all events of type `A` are allocated from a contiguous array, iterating through or batch-processing `A` guarantees near-perfect L1/L2 data cache hit rates.
- **Zero Fragmentation**: Slabs are homogeneously sized based on `sizeof(T)`, completely eliminating the metadata overhead and fragmentation inherent in `dlmalloc` or `jemalloc`.

## 3. O(1) Dispatch Integration
The `runtime_domain` tightly integrates with the `pipeline` dispatcher and `conduit` structures.

When an ingress node parses a network frame:
1. It requests a `managed_data<TargetEvent>` from the local `runtime_domain`.
2. The domain extracts the memory from the mathematically correct type-pool in O(1) time without any `switch` or `if/else` branching.
3. The payload is constructed in-place (placement-new) and fired down the `spsc_ring_conduit`.

## 4. Compile-Time Sovereignty
If a developer attempts to allocate or route an event type that was not explicitly declared in the `runtime_domain`'s blueprint, the compilation engine generates a hard `static_assert` failure. This prevents rogue heap allocations from slipping into the deterministic hot path unnoticed.