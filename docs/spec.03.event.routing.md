# Event Routing (Conduit & Pipeline)

The routing matrix is responsible for the deterministic orchestration of events across the processing graph.

## `slabflux::conduit<Event, Size>`
An ultra-low-latency, lock-free, Single-Producer Single-Consumer (SPSC) ring buffer.
* **O(1) Locality:** Facilitates instantaneous communication across parallel threads and distinct NUMA nodes.
* **Memory Visibility:** Adheres to strict memory consistency models, employing `std::memory_order_release` at the producer tail and `std::memory_order_acquire` at the consumer head.

## `slabflux::net::network_conduit<Event, Size>`
Projects the in-memory Conduit abstraction across physical TCP boundaries.
* Executes non-blocking O(1) system calls (`poll_tx`, `poll_rx`) to maintain line-rate network throughput.
* Features hardened connection management and deterministic dead-peer detection (`is_alive()`).

## `slabflux::pipeline<Handlers...>`
A compile-time-fused event dispatch chain.
* Resolves event typologies instantaneously via template metaprogramming.
* Unmatched events are safely bypassed and their memory is synchronously reclaimed by the pool.

## Deep Dive: Conduit & Pipeline Invariants
* **SPSC Physics:** The `conduit` utilizes C++20 `<bit>` primitives for power-of-two sizing and bitwise masking, paired with `std::hardware_constructive_interference_size` for absolute cache-line isolation. This proprietary architecture enforces hardware-level sovereignty and intentionally departs from canonical open-source ring buffer patterns.
* **Pipeline SFINAE Dispatch:** The `pipeline` leverages variadic template expansion to unroll handlers at compile time. Using SFINAE (Substitution Failure Is Not An Error), it statically guarantees whether a handler's `on(event_ptr<T>&)` signature satisfies the incoming event. This entirely eradicates the runtime overhead associated with `dynamic_cast` or vtable lookups.

## Advanced Routing Components
* **`slabflux::bridge::shared_state_buffer`:** A wait-free synchronization buffer allowing the global propagation of configuration or market state across all cores without mutex contention.
* **`slabflux::bridge::round_robin_switch`:** A wait-free, O(1) load balancer that symmetrically shards high-velocity event streams across multiple downstream conduits.
* **`slabflux::bridge::engine_pulse`:** A precision cross-thread metronome, synchronizing multi-stage processing phases (e.g., I/O batching) globally across the RTE.
