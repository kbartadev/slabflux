# SLABFLUX Architecture: Master Technology Index

This document serves as the root navigation for the SlabFlux 3-Tier Documentation Model. Every module is strictly divided into an architectural component overview, a brief blueprint for API referencing, and a foundation document detailing mathematical and scientific proofs.

## Domains

### 1. [I/O & Hardware Abstraction Layer (IO)](./io/io.index.md)
Contains zero-copy kernel bypass mechanics, POSIX fallbacks, hardware parsing state-machines, and IPC bridging.
### 2. [Networking & Topological Routing (NET)](./net/net.index.md)
Contains the deterministic Directed Acyclic Graph (DAG) logic, SPSC conduits, protocol boundary validations, and sequence logging.
### 3. [Deterministic Compute Matrix (CORE)](./core/core.index.md)
Contains the branchless business logic engines and deterministic state-reconstruction (replay) sagas.
### 4. [Silicon & Cache Intrinsics (HW)](./hw/hw.index.md)
Contains strict hardware-level compiler intrinsics for managing L1/L2/L3 cache eviction and behavior.
### 5. [Mathematical Primitives (ALGO)](./algo/algo.index.md)
Contains O(1) mathematical algorithms executed purely within general-purpose registers without branching.