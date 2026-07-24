# SlabFlux Runtime Engine: Systems Engineering Pathway

Welcome to the SlabFlux tutorial series. This pathway is strictly designed for HFT developers, distributed systems architects, and systems programmers integrating the SlabFlux Runtime Engine into production environments.

SlabFlux rejects traditional asynchronous paradigms—futures, coroutines, mutexes, and driver-mediated hardware offload. Instead, it relies on compile-time unrolling, $O(1)$ latency guarantees, and hardware-isomorphic state transitions.

## Core Learning Modules

### 100-Level: Fundamentals & Determinism
*   **[Tutorial 01: The Sovereign Core & In-Place Topologies](tut.01.sovereign.core.md)**
    Understand the Axiom of In-Place Identity. Learn how SlabFlux projects inheritance views without copying, moving, or dynamically casting memory.
*   **[Tutorial 02: Deterministic Unrolling & The 7D Dispatcher](tut.02.deterministic.unrolling.md)**
    Master Cartesian unrolling, the Inverse Priority Poset, and Diamond Linearization. Eliminate runtime branching from your hot paths.

### 200-Level: Memory Topologies
*   **[Tutorial 03: Memory Topologies & Context Vaults](tut.03.memory.vaults.md)**
    Bind environments using `REGISTER_CONTEXT`. Learn how context extraction is reduced to $O(1)$ compile-time memory offsets, preventing parameter bloat.
*   **Tutorial 04: Lock-Free Conduits & Phase Sync**
    Replace Compare-And-Swap (CAS) spinning and mutexes with SPSC/MPMC lock-free conduits utilizing Monotonic Phase Matching.

### 300-Level: Hardware Isomorphism
*   **Tutorial 05: Hardware Isomorphism & SSDS Offload**
    Bypass CUDA, Vulkan, and kernel-mode drivers. Write directly to the CPU's Write-Combining Buffers (WCB) using AVX-512 to trigger autonomous GPU execution.
*   **[Tutorial 06: Zero-Copy Networking & Causal Meshes](tut.06.zero.copy.net.md)**
    Bridge DPDK, AF_XDP, and SmartNIC FPGA offloads securely into your execution manifold without touching `memcpy`.

### 400-Level: Orchestration & Integrity
*   **Tutorial 07: Execution Halts & Circuit Arbitration**
    Design deterministic `bool`-returning halting sequences to protect downstream state integrity at $O(1)$ latency.
*   **Tutorial 11: Zero-Blocking I/O (Durable Journaling)**
    Integrate `io_uring` with your pipelines for zero-blocking audit logs.

### 500-Level: Production-Grade
*   **Tutorial 08: Snapshot Engines & Sovereign State Recovery**
    Serialize your deterministic Context Vaults asynchronously for microsecond cluster failover.
*   **[Tutorial 09: MPMC Conduits & Mesh Contention](tut.09.mpmc.conduits.md)**
    Handle multi-producer ingress into the Sovereign Core safely without mutexes.
*   **[Tutorial 10: Distributed Determinism & Epoch Aliasing](tut.10.distributed.determinism.md)**
    Synchronize state machines across the Causal Mesh to guarantee identical outcomes on disparate physical nodes.

---

**Prerequisites:** 
- Advanced Modern C++20 (Template Metaprogramming, Traits, Concepts)
- Systems Architecture (Cache hierarchies, Virtual Memory, x86-64 TSO)
- Understanding of Directed Acyclic Graphs (DAGs)