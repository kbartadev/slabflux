# SLABFLUX Architectural Decision Records (ADR)

---

# Architectural Decision Records (ADR) Index

Records of the critical design choices and trade-offs made during the development of SLABFLUX.

* **[ADR 001: Reject OOP Inheritance](./001_REJECT_OOP_INHERITANCE.md)**: Moving away from virtual inheritance and dynamic dispatch in favor of C++20 Concepts and structural recognition.
* **[ADR 002: Union-Based Slab Pool](./002_UNION_BASED_SLAB_POOL.md)**: Transitioning to `union` structures to eliminate strict aliasing violations and `reinterpret_cast` overhead.
* **[ADR 004: Hybrid Synchronization Topology](./003_HYBRID_SYNC_TOPOLOGY.md)**: Enabling MPSC/MPMC queues for control-plane logic while maintaining strict SPSC constraints for the data-plane hot-path.
* **[ADR 005: Explicit Thread-Affinity Strategy](./004_THREAD_AFFINITY_STRATEGY.md)**: Enforcement of core pinning for critical consumers to prevent cache-line invalidation and OS-level jitter.
* **[ADR 006: Pointer-Centric Lifecycle Management](./005_LIFECYCLE_MANAGEMENT.md)**: Mandating manual raw pointer management to eliminate atomic overhead associated with smart pointers in the hot-path.
* **[ADR 007: SIMD-Aligned Memory Layout](./006_SIMD_ALIGNED_MEMORY.md)**: Mandating 64-byte alignment to enable SIMD vectorization and cache-line efficiency.
* **[ADR 008: Branchless Dispatch Invariant](./007_BRANCHLESS_DISPATCH.md)**: Enforcing branchless logic for all hot-path routing to eliminate pipeline stalls and misprediction penalties.
* **[ADR 009: Deterministic Error Handling](./008_DETERMINISTIC_ERROR_HANDLING.md)**: Exception-free execution paths using explicit return types.
