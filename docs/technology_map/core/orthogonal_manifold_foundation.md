# Foundation: Orthogonal Manifold (`slabflux/core/orthogonal_manifold.hpp`)

## 1. Architectural Justification
Traditional MPMC queues rely on centralized atomic counters (`head` and `tail`) which become immediate bottlenecks in many-core systems due to the MESI cache coherence protocol. Read-For-Ownership (RFO) invalidation storms severely limit scalability. The `orthogonal_manifold` replaces this 1D sequence with a 2D matrix, mathematically guaranteeing that thread contention is scattered across independent cache lines.

## 2. Hardware Implementation Directives
- **Orthogonal Traversal Geometry**: Producers traverse the matrix horizontally (row-major), while consumers traverse vertically (column-major). The mathematical intersection guarantees that a producer and consumer only collide on exactly one cell per traversal cycle.
- **Cache-Line Isolation**: Rows are padded strictly to `alignas(64)` (`std::hardware_destructive_interference_size`), ensuring that a producer iterating through a row has exclusive L1 cache ownership of that specific cache line.
- **Hardware Entropy Dispersion**: Threads compute a localized seed based on their thread ID hash to select their starting axes, naturally dispersing load across the entire manifold without centralized coordination.

## 3. Bibliography & Proofs
1. **Shavit, N., & Touitou, D.** (1995). *Elimination Trees and the Construction of Pools and Stacks*. Proceedings of the 7th annual ACM symposium on Parallel algorithms and architectures (SPAA). (Concepts of spatial dispersion to reduce contention).
2. **McKenney, P. E.** (2017). *Is Parallel Programming Hard, And, If So, What Can You Do About It?*. kernel.org. (Analysis of RFO cache invalidation penalties).
3. **Hendler, D., et al.** (2010). *Flat Combining and the Synchronization-Parallelism Tradeoff*. SPAA. (Spreading synchronization across disjoint memory locations).