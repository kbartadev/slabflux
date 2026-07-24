# Foundation: Cross-Orthogonal Queue (`slabflux/core/cross_orthogonal_queue.hpp`)

## 1. Architectural Justification
An evolution of the orthogonal manifold specifically tailored for MPMC queue semantics. When dozens of threads attempt to synchronize over a single channel, spin-locks and CAS-loops burn CPU cycles and generate excessive heat. The cross-orthogonal grid entirely eliminates the CAS-retry loop by forcing threads to advance their cursors upon collision.

## 2. Hardware Implementation Directives
- **Zero-Spin Lock-Free Progression**: If a `compare_exchange_strong` fails, the thread does not retry on the same atomic variable. It instantly advances to the next cell in its orthogonal path. This creates a true wait-free bound on the producer side, ensuring constant forward progress.
- **Bipartite State Machine**: Cells exist only in `VACUUM` (`nullptr`) or `PLASMA` (`T*`) states. Without sequence numbers or ABA-prone version tags, the synchronization relies entirely on the unidirectionality of state transitions (producers only write `T*`, consumers only write `nullptr`).

## 3. Bibliography & Proofs
1. **Herlihy, M.** (1991). *Wait-free synchronization*. ACM Transactions on Programming Languages and Systems (TOPLAS). (Theoretical foundations of wait-free progression).
2. **Kogan, A., & Petrank, E.** (2011). *Wait-Free Queues with Multiple Enqueuers and Dequeuers*. PPoPP. (Analysis of fast-path progression without CAS-loops).