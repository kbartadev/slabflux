# SlabFlux RTE: Active Environment (`environment.hpp`)

## 1. Architectural Overview
The `active_environment` is the grand orchestrator of the entire SlabFlux execution timeline. It fuses together the disparate lock-free conduits, `io_uring` networking, asynchronous NVMe snapshots, and the deterministic SIMD engine into a single, cohesive sovereignty loop.

## 2. Ignition & Topology Builder
The system prohibits dynamic allocations. The `environment_builder` pre-allocates the entire execution topology upfront:
- Creates `pinned_allocator_spsc` memory pools.
- Configures the `fused_nexus_node` and `clock_node`.
- Resolves NUMA affinity, utilizing `allocate_on_local_node` to keep the AI Engine weights physically near the Compute cores.
- Seals the execution threads, enforcing Intel CAT (`sys::cache_partitioner`) to guarantee L3 Cache reservation.

## 3. The Execution Hot-Zone
The `run_compute()` method is a strict, zero-syscall spin-loop (`_mm_pause`).
- **Segment A (Prioritized Dispatch)**: Ensures Administrative commands and Temporal Ticks override normal data processing, preventing Control-Plane starvation.
- **Segment B (Hardware Shielding)**: Reaps packets from the `bimodal_shield_wiring` using ACI envelopes. Any fragmented payloads trigger the `aphasic_horizon_` for structural decoupling.

## 4. Telemetry and Backpressure
The active environment constantly monitors its own physical pulse:
- The `mesh_congestion_audit` actively checks logic latency (`ema_logic_latency_`). If network parsing or unblocking takes too many CPU cycles, it forces an emergency non-blocking snapshot.
- The `blackbox_recorder` captures aggregate metrics (ingress TSC, logic latency, queue drop counts) and flushes them to NVMe during `halt_and_dump_state()` for post-mortem diagnostics.