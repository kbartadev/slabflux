# SlabFlux RTE: Codebase-Driven System Analysis & Curriculum

## Part 1: Codebase-Driven System Analysis

The following subsystem analysis is extracted strictly from the provided SlabFlux directory structure and architectural specifications.

### Verified Subsystems
*   **`ai/`**: Deterministic machine learning inference. Contains `deterministic_ai_core` (in-memory, branchless AI evaluation) and `cognitive_stimulus` (telemetry to tensor conversion).
*   **`bridge/`**: Cross-lane routing and synchronization. Contains `shared_state_buffer`, `round_robin_switch` (O(1) lock-free fan-out), and `engine_pulse`.
*   **`compute/`**: SIMD-accelerated math and logic. Contains `vector_lane_engine`, `vector_lane_512`, `vector_lane_256`, `physics_reactor`, `deterministic_rng`, `long_entropy_rng`, and compile-time routing matrices (`hierarchical_dispatch_chain`, `mixed_dispatch_matrix`).
*   **`core/`**: Foundational O(1) structures. Contains memory management (`spsc_pool`, `mpmc_pool`, `pinned_allocator_spsc`, `pinned_allocator_mpmc`, `huge_slab_pool`, `slab_allocator`, `eternal_memory`), communication (`spsc_conduit`, `mpmc_conduit`, `cross_orthogonal_queue`), execution routing (`pipeline`, `pipeline_lane`, `branchless_engine`), timeline management (`sequence_generator`, `timing_wheel`, `sf_node_ctx`, `hole_puncher`), HA cluster control (`failover_orchestrator`, `snapshot_engine`), memory constraints (`physical_layout`, `cache_shield`, `hardware_topology`), string handling (`fixed_string`, `smart_string`, `string_service`), and basic types (`event`, `offset_ptr`, `managed_data`).
*   **`dist/`**: Distributed causal mesh logic. Implementation details of specific headers are unknown—not inferable from the provided codebase.
*   **`hft/`**: Trading-specific gateways. Contains `chicago_gateway`. Further implementation details unknown.
*   **`hw/`**: Hardware-isomorphic offload boundaries. Contains `isomorphic_matrix_bridge`, `evaluating_cell` (GPU/ALU state transitions), and `signal_multiplexer` (hardware-level pointer broadcast).
*   **`io/`**: Bare-metal input/output bypass. Contains `io_uring_ingress` and interfaces to `durable_journal`.
*   **`net/`**: OS-Bypass TCP/IP Virtual Socket stack. Contains `tcp_flow_engine`, `virtual_tcp_socket`, `virtual_tcp_listener`, `tcp_gateway`, `tcp_wire_engine`, and `tcp_retransmit_timer`.
*   **`rte/`**: Runtime Engine orchestration. Contains `active_environment`, `event_arbiter`, `orthogonal_error_arbiter`, and `flow_controller` (PI/PID-style backpressure).
*   **`supplemental/`**: Testing architectures. Contains `chaos_engine` for deterministic fault injection.
*   **`sys/`**: Operating system boundaries. Contains `topology_enforcer` and `cache_partitioner` (Intel CAT isolation).
*   **`transport/`**: Network protocol processing. Contains `baremetal_parser`, `http_avx_parser`, `http_producer`, `baremetal_json_parser`, `json_producer`, and `json_simd_utils`.
*   **`workflow/`**: State mechanics. Contains `state_machine` (used by failover orchestration).

### Unverified Subsystems
The following directories are standard in large repositories, but their exact header files and mechanical behaviors are **Unknown — not inferable from the provided codebase**:
*   `automation/`
*   `mgmt/`
*   `mesh/`
*   `orchestration/`
*   `pipeline/` (Pipeline mechanics reside in `core/pipeline.hpp`)
*   `platform/` (Platform abstractions exist in `core/hardware_topology.hpp`)
*   `reactor/` (Reactor mechanics reside in `compute/physics_reactor.hpp`)
*   `runtime/` (Domain mapping exists in `core/runtime_domain.hpp`)
*   `security/`
*   `storage/` (Storage mechanisms reside in `core/durable_journal.hpp` and `io/`)

---

## Part 2: Top-Level Tutorial Categories

These categories map 1:1 to the verified subsystems of the SlabFlux architecture.

1.  **Core Memory & Routing Mechanics** (Maps to `core/`, `bridge/`)
2.  **Runtime Environment & Orchestration** (Maps to `rte/`, `workflow/`, `sys/`)
3.  **Vectorized Compute & AI** (Maps to `compute/`, `ai/`)
4.  **Hardware Isomorphism & Telemetry** (Maps to `hw/`, `telemetry/`)
5.  **I/O, Transport, & Networking** (Maps to `io/`, `net/`, `transport/`)
6.  **Supplemental Resilience** (Maps to `supplemental/`)
7.  **Advanced Gateway & Domain Computing** (Maps to `hft/`, `transport/`, `compute/`)
8.  **Timeline Management & String Mechanics** (Maps to `core/`)

---

## Part 3: Tutorials per Category

### Category 1: Core Memory & Routing Mechanics
*   **Foundation: Lock-Free & Wait-Free Topologies**
    *   Path: `docs/technology_map/core/lockfree_concurrency_foundation.md`
    *   Focus: The academic proof and hardware justification for abandoning CAS loops, locks, and mutexes.
*   **Tutorial 1.1: O(1) Memory Leasing**
    *   Modules: `core/spsc_pool.hpp`, `core/pinned_allocator_spsc.hpp`, `core/managed_data.hpp`
    *   Focus: Instantiating HugePage-backed pools and controlling object lifecycles without `std::shared_ptr` or heap fragmentation.
*   **Tutorial 1.2: Deterministic Dispatch & Context Vaults**
    *   Modules: `core/pipeline.hpp`, `core/context_vault.hpp`
    *   Focus: Building Cartesian $N \times M$ unrolled pipelines and injecting static state into handlers.
*   **Tutorial 1.3: Advanced Routing Matrices**
    *   Modules: `bridge/round_robin_switch.hpp`, `core/cross_orthogonal_queue.hpp`
    *   Focus: Sharding event loads symmetrically to worker threads without relying on locks or CAS-loop degradation.

### Category 2: Runtime Environment & Orchestration
*   **Foundation: Causal Determinism & State Machine Replication**
    *   Path: `docs/technology_map/core/causal_determinism_foundation.md`
    *   Focus: Lamport clocks, epoch sequencing, and eliminating physical time to guarantee distributed consistency.
*   **Tutorial 2.1: The Sovereign Ignition Phase**
    *   Modules: `core/sovereign_core.hpp`, `rte/active_environment.hpp`, `sys/topology_enforcer.hpp`
    *   Focus: Bootstrapping the environment, sealing the binary, and executing thread-to-NUMA core pinning.
*   **Tutorial 2.2: Event Arbitration & Backpressure**
    *   Modules: `rte/event_arbiter.hpp`, `rte/flow_controller.hpp`, `core/backpressure_valve.hpp`
    *   Focus: Enforcing the 5-Strike starvation rule and deploying High/Low watermark throttling for network ingress.
*   **Tutorial 2.3: Cluster Failover Orchestration**
    *   Modules: `core/failover_orchestrator.hpp`, `workflow/state_machine.hpp`
    *   Focus: Managing Active-Passive cluster states, Gratuitous ARP spoofing, and Split-Brain prevention via nexus locks.

### Category 3: Vectorized Compute & AI
*   **Tutorial 3.1: Branchless Vector Lanes**
    *   Modules: `compute/vector_lane_engine.hpp`, `compute/vector_lane_512.hpp`
    *   Focus: Writing mathematical transformations that execute 16-to-32 array elements concurrently using `_mm512_load_si512`.
*   **Tutorial 3.2: Deterministic Cognitive Inference**
    *   Modules: `ai/deterministic_ai_core.hpp`, `ai/cognitive_stimulus.hpp`
    *   Focus: Loading static expert weights and transforming mesh network telemetry into AVX-compatible tensor formats.

### Category 4: Hardware Isomorphism & Telemetry
*   **Foundation: Systolic Spatial Dataflow Substrate**
    *   Path: `docs/technology_map/hw/systolic_dataflow_substrate_foundation.md`
    *   Focus: Discarding the Von Neumann architecture for static, data-driven spatial compute graphs on the GPU.
*   **Tutorial 4.1: Systolic Dataflow (SSDS) Offload**
    *   Modules: `hw/isomorphic_matrix_bridge.hpp`, `hw/evaluating_cell.hpp`
    *   Focus: Eliminating CUDA driver context switches by streaming 64-byte emission slots directly to persistent GPU ALUs via PCIe WCB.
*   **Tutorial 4.2: Hardware Signal Multiplexing**
    *   Modules: `hw/signal_multiplexer.hpp`
    *   Focus: Using C++17 Fold Expressions to unroll pointer duplication across parallel physical channels.

### Category 5: I/O, Transport, & Networking
*   **Foundation: Kernel Bypass & Zero-Copy Networking**
    *   Path: `docs/technology_map/transport/kernel_bypass_foundation.md`
    *   Focus: Bypassing POSIX sockets, interrupts, and ring-transitions via DPDK, AF_XDP, and io_uring.
*   **Foundation: User-Space TCP/IP & OS-Bypass Sockets**
    *   Path: `docs/technology_map/net/tcp_os_bypass_foundation.md`
    *   Focus: Why textbook TCP state machines fail at HFT speeds, and how bitmask states and structural fusion replace them.
*   **Tutorial 5.1: Zero-Syscall Kernel Bypass**
    *   Modules: `io/io_uring_ingress.cpp`, `net/network_conduit.hpp`
    *   Focus: Polling submission/completion queues entirely in user-space via SQPOLL mechanisms.
*   **Tutorial 5.2: OS-Bypass TCP/IP & Virtual Sockets**
    *   Modules: `net/tcp_flow_engine.hpp`, `net/virtual_tcp_socket.hpp`, `net/tcp_gateway.hpp`
    *   Focus: Managing TCP handshakes, congestion windows, and sequence boundaries without relying on the Linux kernel network stack.
*   **Tutorial 5.3: Durable Hardware Journaling**
    *   Modules: `core/durable_journal.hpp`
    *   Focus: Opening block devices with `O_DIRECT` and bypassing the Linux Page Cache to persist LSN states.

### Category 6: Supplemental Resilience
*   **Tutorial 6.1: Deterministic Fault Injection**
    *   Modules: `supplemental/chaos_engine.hpp`, `compute/deterministic_rng.hpp`
    *   Focus: Simulating hardware drops and cache-line corruption on the hot path.
*   **Tutorial 6.2: Out-of-Order Quarantine**
    *   Modules: `core/hole_puncher.hpp`
    *   Focus: Reordering UDP multicast packets in O(1) time using hardware bit-scanning.
*   **Tutorial 6.3: Intel CAT Cache Isolation**
    *   Modules: `sys/cache_partitioner.hpp`
    *   Focus: Shielding the Sovereign Core's L3 cache from noisy neighbors using Intel RDT.

### Category 7: Advanced Gateway & Domain Computing
*   **Tutorial 7.1: Zero-Copy Protocol Parsing**
    *   Modules: `transport/baremetal_parser.hpp`, `transport/http_avx.hpp`, `transport/baremetal_json_parser.hpp`
    *   Focus: Unrolling network protocol deserialization directly off the hardware buffer using stateful DFAs and AVX-accelerated sieves.
*   **Tutorial 7.2: Zero-Allocation Protocol Production**
    *   Modules: `transport/http_producer.hpp`, `transport/json_producer.hpp`
    *   Focus: Serializing payloads directly onto L1 cache-aligned outbound DMA network conduits.
*   **Tutorial 7.3: Deterministic Financial Gateways**
    *   Modules: `hft/chicago_gateway.hpp`
    *   Focus: Mapping normalized pipeline telemetry to exchange-specific outbound formats.
*   **Tutorial 7.4: High-Frequency Physics Reactors**
    *   Modules: `compute/physics_reactor.hpp`
    *   Focus: Managing stateful kinematic or risk models entirely within L1 cache boundaries.

### Category 8: Timeline Management & String Mechanics
*   **Tutorial 8.1: Jitter-Free Timers & Sequencing**
    *   Modules: `core/timing_wheel.hpp`, `core/sequence_generator.hpp`
    *   Focus: Polling $O(1)$ timing wheels on the hot path to avoid OS scheduler interrupts and sleeping.
*   **Tutorial 8.2: Deterministic String Handling**
    *   Modules: `core/fixed_string.hpp`, `core/smart_string.hpp`, `core/string_service.hpp`
    *   Focus: Eliminating `std::string` heap allocations and avoiding virtual memory page faults during text formatting.

---

## Part 4: Hands-On Exercises

1.  **Exercise: Lock-Free Pipeline Transfer**
    *   *Task*: Create a `slabflux::core::spsc_pool`, generate a raw event pointer, route it through a `slabflux::core::spsc_conduit`, and safely invoke `pool.release(ptr)` on the receiving end.
2.  **Exercise: Wait-Free Ingress Sharding**
    *   *Task*: Utilize `slabflux::bridge::round_robin_switch` to distribute a stream of 1,000,000 integers evenly across four downstream `mpmc_conduit` queues without a single failed CAS loop.
3.  **Exercise: Sub-Nanosecond Telemetry Validation**
    *   *Task*: Query the CPU Time Stamp Counter via `__rdtsc()`, execute a dummy pipeline loop, and measure the exact hardware cycle latency of the iteration.
4.  **Exercise: Out-of-Order Quarantine**
    *   *Task*: Instantiate a `slabflux::core::hole_puncher`. Inject LSN 2, LSN 4, and LSN 5. Verify they are quarantined via the `presence_mask`. Inject LSN 3 and trigger the `__builtin_ctzll` cascade flush.

---

## Part 5: The Learning Path

### Stage 1: Beginner (Foundations of Determinism)
*   **Dependencies**: `core/` (Memory, Routing, Invariants)
*   **Goals**: Discard OOP habits. Learn to allocate fixed memory (`spsc_pool`), construct stateless struct events, and unroll execution via `pipeline.hpp`. Understand the axiom of In-Place Identity.

### Stage 2: Intermediate (Concurrency & Topologies)
*   **Dependencies**: `bridge/`, `rte/`, `core/hardware_topology.hpp`
*   **Goals**: Transition to multi-core architectures. Learn `mpmc_conduit` phase-tag synchronization, connect lanes using `round_robin_switch`, and bind threads to explicit silicon using `topology_enforcer`.

### Stage 3: Advanced (Hardware Compute & I/O)
*   **Dependencies**: `compute/`, `io/`, `transport/`
*   **Goals**: Exploit hardware physics. Convert scalar loops to `vector_lane_engine` AVX-512 operations. Implement asynchronous disk journaling via `io_uring_ingress` and `O_DIRECT` mapping to avoid OS system calls.

### Stage 4: Expert (Orchestration & Hardware Offload)
*   **Dependencies**: `hw/`, `ai/`, `core/failover_orchestrator.hpp`, `core/hole_puncher.hpp`
*   **Goals**: Master cluster sovereignty and heterogeneous compute. Offload matrices via `isomorphic_matrix_bridge` without kernel drivers. Handle split-brain prevention, causal mesh reordering (`hole_puncher`), and instantaneous Active-Passive role handoffs.