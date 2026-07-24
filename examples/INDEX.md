# SLABFLUX: Code Examples

The examples are categorized by complexity and architectural layer, providing a hands-on guide to mastering deterministic, allocation-free event-driven systems.

### 📂 01_core_patterns
Fundamental building blocks and structural idioms.
* **[Cascading Pipelines](./01_core_patterns/01_cascading_pipeline.cpp)**: Composition-based event processing without OOP fragmentation.
* **[C++20 Concepts](./01_core_patterns/02_cxx20_concepts.cpp)**: Structural recognition of events without common base classes.

### 📂 02_concurrency_and_routing
Mastering the flux between hardware cores.
* **[SPSC Bus](./02_concurrency_and_routing/01_lockfree_spsc_bus.cpp)**: Contention-free, lock-free communication between threads.
* **[Fan-In Polling](./02_concurrency_and_routing/02_fan_in_polling.cpp)**: Fair event extraction from multiple sources using the `round_robin_poller`.
* **[Sharding](./02_concurrency_and_routing/03_sharding.cpp)**: Deterministic load balancing using the `round_robin_switch`.

### 📂 03_resilience_and_control
Operational safety and system integrity under maximum load.
* **[Backpressure Drops](./03_resilience_and_control/01_backpressure_drops.cpp)**: Managing buffer overflows deterministically without blocking.
* **[Heartbeat Timeouts](./03_resilience_and_control/02_heartbeat_timeouts.cpp)**: Sub-millisecond connection state and lifecycle tracking.
* **[Graceful Drain](./03_resilience_and_control/03_graceful_drain.cpp)**: Safe, leak-free pipeline teardowns enforcing raw pointer lifecycle rules.
* **[Cache Line Isolation](./03_resilience_and_control/04_cache_line_isolation.cpp)**: Preventing false-sharing across CPU and NUMA nodes.

### 📂 04_showcase
Production-ready architectural topologies demonstrating the engine's general-purpose flexibility.
* **[Deterministic Network Routing](./04_showcase/01_deterministic_network_routing.cpp)**: A high-performance Layer-7 packet gateway.
* **[Zero Allocation Hotpath](./04_showcase/02_zero_allocation_hotpath.cpp)**: End-to-end flow with absolutely zero OS-level heap usage.
* **[Compile-Time Tag Demuxing](./04_showcase/03_compile_time_tag_demuxing.cpp)**: Ultra-low latency risk validation engine featuring automated type-to-ID compile-time extraction, explicit cache-line isolation, and branchless jump-table (fold expression) routing.
* **[Autonomous Drone ECS](./04_showcase/04_autonomous_drone_ecs.cpp)**: Using the framework for high-tickrate telemetry and spatial control.
* **[Ultimate Logic Synthesis](./04_showcase/05_ultimate_logic_synthetis.cpp)**: Enterprise pipeline incorporating Native Multiple Inheritance and Matrix Routing.
* **[Bimodal Integration](./04_showcase/06_bimodal_hft_integration.cpp)**: Managing high-frequency data ingestion alongside slower stateful logic seamlessly.

### 📂 05_system_stress_audits
Demonstrating deterministic core capabilities and line-rate thresholds under high-pressure synthetic scenarios.
* **[HFT Ingress Parsing Showcase](./05_system_stress_audits/01_hft_benchmark.cpp)**: Validates end-to-end zero-copy event ingestion, static compile-time pipeline dispatch, and sub-microsecond signal generation latency.
* **[Conduit Saturation Showcase](./05_system_stress_audits/02_bridge_sync_benchmark.cpp)**: A side-by-side throughput audit demonstrating lock-free SPSC thread-boundary crossings and cache-line isolation under maximum saturation.
