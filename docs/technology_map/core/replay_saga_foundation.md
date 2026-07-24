# Foundation: Replay Saga (`slabflux/core/replay_saga.hpp`)

## 1. Architectural Justification
The `replay_saga` is a deterministic state reconstruction engine. It processes a durable NVMe log file to recreate the exact bit-perfect execution state of the `branchless_engine`, enabling identical replicas for crash recovery, failover, or forensic analysis.

## 2. Hardware Implementation Directives
- **Zero-Syscall File Ingestion**: Abandons sequential `read()` syscalls in favor of a single `mmap` spanning the entire file size with `MAP_POPULATE` and `MAP_SHARED`. This pre-faults the memory and eliminates Page Fault micro-stalls during the replay sequence.
- **Bit-Perfect State Reconstruction**: Reploys historical LSNs directly into the scalar/SIMD `vector_lane_engine`, reproducing the exact register states identically to the active cluster node.
- **Silicon Priming**: Integrates with the `replay_manager` to train the Branch Target Buffer (BTB) with dummy events ("Ghost Events") prior to unlocking live network ingress, preventing cold-cache latency spikes on the first live packet.

## 3. Bibliography & Proofs
1. **Mohan, C., et al.** (1992). *ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging*. ACM Transactions on Database Systems.
2. **LMAX Exchange**. (2011). *The LMAX Architecture*. (Deterministic state reconstruction via Event Sourcing).
3. **Gorman, M.** (2004). *Understanding the Linux Virtual Memory Manager*. (mmap optimizations and page pre-faulting).