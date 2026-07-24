# Foundation: Sovereign Node Context (`slabflux/core/sf_node_ctx.hpp`)

## 1. Architectural Justification
To maintain a strict, replayable timeline across a distributed system, every thread and logical core must be perfectly aware of its chronological position. The `sf_node_ctx` acts as the localized temporal anchor for individual execution pipelines without inducing global lock contention.

## 2. Hardware Implementation Directives
- **Logical Sequence Tracking**: Maintains the absolute LSN (Logical Sequence Number) and the verified Commit Horizon locally within the pipeline's execution loop, avoiding cross-core cache invalidations (RFO).
- **Gap Analysis & Replay**: During failover, queries the `sf_node_ctx` to establish the last known-good LSN marker. The `replay_manager` injects historical events perfectly from this coordinate, preventing duplicated side-effects.
- **Thread Pinning Context**: Securely caches the hardware topology bindings (NUMA node ID, physical core ID). Prevents redundant OS queries (e.g., `sched_getcpu()`) during hot-path NUMA-aware allocations.

## 3. Bibliography & Proofs
1. **Drepper, U.** (2007). *What Every Programmer Should Know About Memory*. (Thread local storage vs localized memory cache-line access).
2. **Boyd-Wickizer, V., et al.** (2010). *An Analysis of Linux Scalability to Many Cores*. OSDI. (The overhead of OS topology queries).
3. **LMAX Exchange**. (2011). *The LMAX Architecture*. (Sequence tracking without global memory contention).