# Mastery: SlabFlux Implementation Tracks

This index provides a structured path for engineers to master the deterministic mechanics of the SlabFlux Runtime Engine. Each track moves from high-level C++20 abstractions down to the physical properties of the silicon.

## Track 1: Compile-Time Foundations

### 01. The Zero-Overhead Philosophy & C++20 Concepts
* **File:** [`tut.01.concepts.and.pipelines.md`](./tut.01.concepts.and.pipelines.md)
* **Focus:** Completely eliminating traditional OOP inheritance based on virtual function tables (`vtable`). Structural event recognition (duck typing) using C++20 Concepts (`requires` clauses) with absolutely zero runtime cost.

### 02. The Pool Lifecycle & Perfect Forwarding
* **File:** [`tut.02.memory.pool.in.practice.md`](./tut.02.memory.pool.in.practice.md)
* **Focus:** Deterministic memory management using the fixed-size, lock-free `spsc_pool`. The lifecycle of raw pointers (`T*`) on the hot path after the removal of `event_ptr`. In-place object construction with perfect forwarding semantics (via the `make()` call) and the iron rules of manual terminal `release()`.

### 03. Building Cascading Pipelines
* **File:** [`tut.03.clusters.and.routing.md`](./tut.03.clusters.and.routing.md)
* **Focus:** Composition over inheritance (Flattened Data Layout). Constructing flat, cache-friendly, tightly packed POD data layers, and their strictly compile-time–inlined, waterfall-style execution chain.

### 04. Zero-Allocation Strings in the Hot Path
* **File:** [`tut.04.string.management.md`](./tut.04.string.management.md)
* **Focus:** Handling dynamic strings and textual data without heap allocation or fragmentation. The fixed-size `fixed_string<N>` for network frames, and the developer-friendly `smart_string` built on lock-free chunk chaining, integrated through the `string_service`.

## Track 2: Mechanical Concurrency

### 05. Lock-Free Concurrency & the SPSC Bus
* **File:** [`tut.05.lockfree.concurrency.md`](./tut.05.lockfree.concurrency.md)
* **Focus:** Lightning-fast communication between threads without mutexes, locks, or context switches. Zero-copy transfer of raw pointers through the hardware‑optimized `spsc_conduit` ring buffers.

### 06. Backpressure & Deterministic Packet Dropping
* **File:** [`tut.06.backpressure.management.md`](./tut.06.backpressure.management.md)
* **Focus:** Handling overflow in fixed-size resources. Backpressure and deterministic event dropping under overload. How to prevent the Pool from being depleted when raw pointers are discarded.

### 07. Graceful Drain & The Poison Pill Pattern
* **File:** [`tut.07.graceful.drain.md`](./tut.07.graceful.drain.md)
* **Focus:** Clean and leak‑free shutdown of parallel, concurrent O(1) pipelines when operating‑system‑level interrupts (SIGINT / Ctrl+C) occur, using the asynchronous “Poison Pill” mechanism.

### 08. High-Scale Routing Patterns (Fan-In / Fan-Out)
* **File:** [`tut.08.high.scale.routing.md`](./tut.08.high.scale.routing.md)
* **Focus:** Scaling without slow MPSC (Multi‑Producer) queues. Fair, lock‑free polling from multiple input sources using `round_robin_poller` (Fan‑In), and deterministic load distribution across worker threads using `round_robin_switch` (Sharding / Fan‑Out).

## Track 3: Physical Optimization

### 09. Hardware Topology & Cache Line Isolation
* **File:** [`tut.09.hardware.topology.md`](./tut.09.hardware.topology.md)
* **Focus:** Completely eliminating CPU core invalidation (False Sharing) through strict `alignas(64)` cache-line alignment. NUMA-aware (Non-Uniform Memory Access) local memory allocation and thread pinning via the `hardware_topology` layer.

### 10. SIMD Compute & Vector Lanes
* **File:** [`tut.10.simd.compute.md`](./tut.10.simd.compute.md)
* **Focus:** Deterministic execution without conditional branching (`if-else`). Parallel dataflow mutation using AVX2 and AVX‑512 hardware CPU instructions (intrinsics) through the `vector_lane_engine`.

### 11. Zero-Blocking I/O & Durable Journaling
* **File:** [`tut.11.zero.blocking.io.md`](./tut.11.zero.blocking.io.md)
* **Focus:** Writing to disk and audit logging without stalling the hot path even for a single nanosecond. Avoiding blocking kernel system calls using Linux `io_uring` (in SQPOLL mode) and integrating the asynchronous `durable_sink` pipeline.

### 12. Bimodal Execution Model (Hot Path vs. Cold Path)
* **File:** [`tut.12.bimodal.execution.md`](./tut.12.bimodal.execution.md)
* **Focus:** Complete separation of immediate, strictly deterministic O(1) logic (Hot Path, e.g., physics calculations, network routing) from slow, stateful, or blocking operations (Cold Path, e.g., PostgreSQL persistence, external REST API calls) along lock-free asynchronous boundaries.

## Track 4: Advanced Matrix Operations

### 13. Sub-Nanosecond Timekeeping (TSC)
* **File:** [`tut.13.sub.nanosecond.timekeeping.md`](./tut.13.sub.nanosecond.timekeeping.md)
* **Focus:** Telemetry and micro-latency measurement without expensive operating-system clock calls. Direct querying of the CPU Time Stamp Counter (`__rdtsc()`) hardware register with zero systemic overhead.

### 14. The Network Gateway & Zero-Copy Ingress
* **File:** [`tut.14.network.gateway.md`](./tut.14.network.gateway.md)
* **Focus:** Immediate, zero-copy conversion of network buffers. Direct casting (reinterpret cast) of raw TCP/UDP bytes into strictly aligned C++ POD structures and dispatching them directly from the gateway layer into the logical channel.

### 15. Event Sourcing & Deterministic Replay
* **File:** [`tut.15.deterministic.replay.md`](./tut.15.deterministic.replay.md)
* **Focus:** Reconstructing the system’s full state without persistent databases. Since the hot path is free of side effects and branch drift, replaying the saved events (`replay_saga`) can rebuild a bit-identical state.

### 16. Chaos Engineering & Fault Injection
* **File:** [`tut.16.chaos.engineering.md`](./tut.16.chaos.engineering.md)
* **Focus:** Stress-testing the system’s stability and survivability. Injecting artificial packet drops, network jitter simulations, and packet duplications into the channels using the `chaos_engine` built on the deterministic core.

### 17. The Ephemeral Context Engine
* **File:** [`tut.17.ephemeral.context.md`](./tut.17.ephemeral.context.md)
* **Focus:** Injecting runtime contexts and mutable state into handlers without unnecessarily bloating event memory (Zero Tuple Bloat). 1:1 event‑context mappings with stack‑allocated lifetimes.

### 18. The 4D Matrix Dispatch (Compile-Time Inheritance)
* **File:** [`tut.18.4d.matrix.dispatch.md`](./tut.18.4d.matrix.dispatch.md)
* **Focus:** Hierarchical event‑processing patterns without runtime virtual costs. Deep analysis of the `extends<BaseEvent>` metaprogramming utility and the C++17 Fold Expression–based automatic pointer routing, based on the factory pipeline tests.

### 19. Ownership Stealing & Pipeline Short-Circuiting
* **File:** [`tut.19.ownership.stealing.md`](./tut.19.ownership.stealing.md)
* **Focus:** Taking control away from the framework. How to use the `scoped_ptr<T>&` signature to interrupt (short‑circuit) the pipeline and “rip out” memory ownership for asynchronous offloading or buffering, with zero runtime penalty.

### 20. Compile-Time Tag Demuxing & Transport Decoupling
* **File:** [`tut.20.compile.time.demuxing.md`](./tut.20.compile.time.demuxing.md)
* **Focus:** Decoupling the transport layer (e.g., `tagged_pointer`) from the core execution `pipeline`. Using C++17 fold expressions to build O(1) branchless jump tables that unpack network byte-streams into strictly typed L1-cache events with zero runtime overhead.
