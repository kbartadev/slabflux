# SlabFlux HW: Cache Demotion (`slabflux/hw/cache_demotion.hpp`)

## 1. Architectural Justification
Modern CPU caches utilize LRU (Least Recently Used) policies. In high-throughput network applications, processing a massive burst of incoming data can accidentally evict critical application state (like pricing matrices) from the L1 cache. The cache demotion toolkit explicitly prevents this cache thrashing.

## 2. Hardware Implementation Directives
- **_mm_cldemote**: Instructs the hardware to aggressively move a specific cache line from the L1/L2 cache down to the L3 (Last Level Cache) once it has been processed.
- **Zero-Latency Execution**: Operates as a purely speculative hardware hint. The CPU pipeline does not stall waiting for the eviction to occur, meaning the execution path remains branchless and wait-free.
- **Spatial Targeting**: Only targets cache lines associated with ephemeral network buffers, leaving core strategy state naturally pinned in L1.

## 3. Pipeline Application
Applied exclusively to `wire_frame` payload bytes immediately after the SIMD parsers have extracted the required fields. This guarantees that cold network payload data vacates the L1 cache instantly, preserving space for the hot deterministic logic of the `branchless_engine`.