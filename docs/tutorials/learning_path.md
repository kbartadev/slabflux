# The SlabFlux Learning Path

This document expands on the curriculum outlined in `master_curriculum.md`. It provides a strict, sequential reading order and progression criteria for integrating developers into the SlabFlux Runtime Engine.

---

## Stage 1: Beginner (Foundations of Determinism)
**Objective:** Unlearn standard object-oriented programming (OOP) habits. Stop using heap allocations, virtual functions, and OS-mediated synchronization. Understand the axiom of In-Place Identity and $O(1)$ dispatch.

**Required Reading:**
0.  Foundation: Lock-Free & Wait-Free Topologies
    *   *Concept:* The architectural and academic justification for abandoning CAS loops and OS mutexes.
1.  Tutorial 1.1: O(1) Memory Leasing
    *   *Concept:* `spsc_pool`, `pinned_allocator_spsc`, `managed_data`
2.  Tutorial 1.2: Deterministic Dispatch & Context Vaults
    *   *Concept:* $N \times M$ unrolled `pipeline`, `context_vault`, SFINAE matching
3.  Tutorial 2.1: The Sovereign Ignition Phase
    *   *Concept:* `active_environment`, `sovereign_core`, `topology_enforcer`

**Milestone:** 
Complete **Exercise 1: Lock-Free Pipeline Transfer** in Exercise Solutions. You must be able to allocate an event, pass it across an `spsc_conduit` isolation boundary, and safely reclaim it without locks.

---

## Stage 2: Intermediate (Concurrency & Topologies)
**Objective:** Transition from a single Sovereign Core to multi-core architectures. Route data efficiently, absorb backpressure without yielding to the OS scheduler, and shield your L3 cache from noisy neighbors.

**Required Reading:**
1.  Tutorial 1.3: Advanced Routing Matrices
    *   *Concept:* `round_robin_switch`, `cross_orthogonal_queue`
2.  Tutorial 2.2: Event Arbitration & Backpressure
    *   *Concept:* `event_arbiter`, `flow_controller`, `backpressure_valve`
3.  Tutorial 6.3: Intel CAT Cache Isolation
    *   *Concept:* `cache_partitioner`, Class of Service (CLOS)

**Milestone:**
Complete **Exercise 2: Wait-Free Ingress Sharding**. You must demonstrate the ability to fan-out millions of events symmetrically across hardware queues using bitwise arithmetic instead of locks.

---

## Stage 3: Advanced (Hardware Compute & I/O)
**Objective:** Exploit raw hardware physics. Replace scalar loops with AVX-512 intrinsic operations and bypass the Linux Page Cache and network stack entirely.

**Required Reading:**
0.  Foundation: Kernel Bypass & Zero-Copy Networking
    *   *Concept:* Interrupt-driven I/O degradation and direct memory access (DMA) architectures.
0.  Foundation: User-Space TCP/IP & OS-Bypass Sockets
    *   *Concept:* Replacing OS network stacks with wait-free, $O(1)$ bitmask-driven state machines.
1.  Tutorial 3.1: Branchless Vector Lanes
    *   *Concept:* `vector_lane_engine`, `vector_lane_512`, AVX-512 masking
2.  Tutorial 5.1: Zero-Syscall Kernel Bypass
    *   *Concept:* `io_uring_ingress`, `network_conduit`, SQPOLL
3.  Tutorial 5.2: Durable Hardware Journaling
    *   *Concept:* `durable_journal`, NVMe sector alignment, `O_DIRECT`

**Milestone:**
Complete **Exercise 3: Sub-Nanosecond Telemetry Validation**. You must successfully use `__rdtsc()` and `_mm_lfence()` to profile the exact hardware cycle cost of your pipeline unrolling.

---

## Stage 4: Expert (Orchestration & Hardware Offload)
**Objective:** Master heterogeneous cluster operations. Handle GPU offloads without kernel drivers, execute deterministic AI inference on the hot path, and govern Active-Passive split-brain mitigation.

**Required Reading:**
0.  Foundation: Causal Determinism & State Machine Replication
    *   *Concept:* Logical Sequence Numbers (LSNs) and distributed causal meshes.
0.  Foundation: Systolic Spatial Dataflow Substrate
    *   *Concept:* Static, data-driven spatial compute graphs for autonomous GPU execution.
1.  Tutorial 2.3: Cluster Failover Orchestration
    *   *Concept:* `failover_orchestrator`, `state_machine`
2.  Tutorial 3.2: Deterministic Cognitive Inference
    *   *Concept:* `deterministic_ai_core`, `cognitive_stimulus`
3.  Tutorial 4.1: Systolic Dataflow (SSDS) Offload
    *   *Concept:* `isomorphic_matrix_bridge`, `evaluating_cell`
4.  Tutorial 4.2: Hardware Signal Multiplexing
    *   *Concept:* `signal_multiplexer`, C++17 Fold Expressions
5.  Tutorial 6.1: Deterministic Fault Injection
    *   *Concept:* `chaos_engine`, `deterministic_rng`
6.  Tutorial 6.2: Out-of-Order Quarantine
    *   *Concept:* `hole_puncher`, `__builtin_ctzll`

**Milestone:**
Complete **Exercise 4: Out-of-Order Quarantine**. You must build a hardware-accelerated reorder buffer capable of realigning dropped UDP multicast packets in $O(1)$ time.

---

**Next Steps:** Review the `slabflux/dist/` and `slabflux/hft/` subsystem headers (where internal implementation specifics reside) to apply these deterministic principles directly to your proprietary market gateways.