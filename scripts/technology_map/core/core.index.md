# SLABFLUX Core Technology Map: Index

## 1. Summary Overviews
- [Architectural Blueprint](./core.blueprint.md)
- [Foundational References](./core.foundation.md)

## 2. Memory Management & Physical Topologies
### Pinned & Block Allocators
- [General Blueprint](./core.allocators.blueprint.md) | [General Foundation](./core.allocators.foundation.md)
- [Pinned Slab Blueprint](./core.pinned_slab_allocator.blueprint.md) | [Pinned Slab Foundation](./core.pinned_slab_allocator.foundation.md)
- [Pinned Slab Overview](./core.pinned_allocators.md)
### Distributed Pools
- [Managed Data Blueprint](./core.managed_data.blueprint.md) | [Managed Data Foundation](./core.managed_data.foundation.md)
- [SPSC Pool Blueprint](./core.spsc_pool.blueprint.md) | [SPSC Pool Foundation](./core.spsc_pool.foundation.md)
- [MPSC Pool Blueprint](./core.mpsc_pool.blueprint.md) | [MPSC Pool Foundation](./core.mpsc_pool.foundation.md)
- [MPMC Pool Blueprint](./core.mpmc_pool.blueprint.md) | [MPMC Pool Foundation](./core.mpmc_pool.foundation.md)
- [Hybrid Pool Blueprint](./core.mpsc_hybrid_pool.blueprint.md) | [Hybrid Pool Foundation](./core.pools.foundation.md)
- [Ring Pool Blueprint](./core.spsc_ring_pool.blueprint.md) | [Ring Pool Foundation](./core.pools.foundation.md)
### Memory Residency & Hardening
- [HugePage Blueprint](./core.hugepage_allocator.blueprint.md) | [HugePage Foundation](./core.hugepage_allocator.foundation.md)
- [TLB Warmup Blueprint](./core.tlb_warmup.blueprint.md) | [TLB Warmup Foundation](./core.tlb_warmup.foundation.md)
- [ABA Elimination Blueprint](./core.aba_elimination.blueprint.md) | [ABA Elimination Foundation](./core.aba_elimination.foundation.md)
- [Offset Pointers Blueprint](./core.offset_ptr.blueprint.md) | [Offset Pointers Foundation](./core.offset_ptr.foundation.md)
- [Rank-Aware Allocator Blueprint](./core.rank_aware_allocator.blueprint.md) | [Rank-Aware Foundation](./core.rank_aware_allocator.foundation.md)

## 3. Concurrency & Conduit Routing
### Lock-Free Conduits (Wires)
- [General Blueprint](./core.conduits.blueprint.md) | [General Foundation](./core.conduits.foundation.md)
- [SPSC Conduit Blueprint](./core.spsc_conduit.blueprint.md) | [SPSC Conduit Foundation](./core.spsc_conduit.foundation.md)
- [Ring Conduit Blueprint](./core.spsc_ring_conduit.blueprint.md) | [Ring Conduit Foundation](./core.spsc_ring_conduit.foundation.md)
### Matrix Routing
- [MPMC Conduit Blueprint](./core.mpmc_conduit.blueprint.md) | [MPMC Conduit Foundation](./core.mpmc_conduit.foundation.md)
- [MPMC Sharded Conduit Blueprint](./core.mpmc_sharded_conduit.blueprint.md) | [MPMC Sharded Conduit Foundation](./core.mpmc_sharded_conduit.foundation.md)
### Conduit Management
- [Mesh Routing Blueprint](./core.routing.blueprint.md) | [Mesh Routing Foundation](./core.routing.foundation.md)
- [RR Poller Blueprint](./core.round_robin_poller.blueprint.md) | [RR Poller Foundation](./core.round_robin_poller.foundation.md)
- [RR Switch Blueprint](./core.round_robin_switch.blueprint.md) | [RR Switch Foundation](./core.round_robin_switch.foundation.md)
### Work Orchestration
- [Task Matrix Blueprint](./core.validation_pool.blueprint.md) | [Task Matrix Foundation](./core.validation_pool.foundation.md)
- [Asynchronous Ingress Blueprint](./core.fused_nexus_node.blueprint.md) | [Asynchronous Ingress Foundation](./core.fused_nexus_node.foundation.md)

## 4. Event-Driven Architecture & Dispatch
- [Pipeline Blueprint](./core.pipeline.blueprint.md) | [Pipeline Foundation](./core.pipeline.foundation.md)
- [Demuxer Blueprint](./core.demuxer.blueprint.md) | [Demuxer Foundation](./core.demuxer.foundation.md)
- [Causal Entity Blueprint](./core.causal_entity.blueprint.md) | [Causal Entity Foundation](./core.causal_entity.foundation.md)
- [Logic Gateway Blueprint](./core.gateway.blueprint.md) | [Logic Gateway Foundation](./core.gateway.foundation.md)
- [Context Engine Blueprint](./core.context.blueprint.md) | [Context Foundation](./core.context.foundation.md)

## 5. Micro-Architectural Execution Guards
### Direct Silicon Control
- [FPU Shield Blueprint](./core.fpu_shield.blueprint.md) | [FPU Shield Foundation](./core.fpu_shield.foundation.md)
- [Execution Guards Blueprint](./core.execution_guards.blueprint.md) | [Execution Guards Foundation](./core.execution_guards.foundation.md)
### Cache & Symbol Sovereignty
- [Cache Isolation Blueprint](./core.cache_shield.blueprint.md) | [Cache Isolation Foundation](./core.cache_shield.foundation.md)

## 6. Hardware Awareness & System Services
### Topology & Instrumentation
- [Hardware Topology Blueprint](./core.hardware_topology.blueprint.md) | [Hardware Topology Foundation](./core.hardware_topology.foundation.md)
- [Topology Traits Blueprint](./core.topology_traits.blueprint.md) | [Topology Traits Foundation](./core.topology_traits.foundation.md)
### Integrity & Scheduling
- [LSN Engine Blueprint](./core.lsn_engine.blueprint.md) | [LSN Engine Foundation](./core.lsn_engine.foundation.md)
- [Timing Wheels Blueprint](./core.timing_wheel.blueprint.md) | [Timing Wheels Foundation](./core.timing_wheel.foundation.md)
- [Integrity Watchdog Blueprint](./core.integrity_watchdogs.blueprint.md) | [Integrity Watchdog Foundation](./core.integrity_watchdogs.foundation.md)
- [Hotpatching Blueprint](./core.hotpatch_bridge.blueprint.md) | [Hotpatching Foundation](./core.hotpatch_bridge.foundation.md)
- [Policing & Flow Blueprint](./core.policing.blueprint.md) | [Policing & Flow Foundation](./core.policing.foundation.md)
### State Management
- [Snapshot Manager Blueprint](./core.snapshot_manager.blueprint.md) | [Snapshot Foundation](./core.snapshot_manager.foundation.md)
- [Replay Saga Blueprint](./core.replay_saga.blueprint.md) | [Replay Foundation](./core.replay_saga.foundation.md)

## 7. Sub-Microsecond Primitives
- [Smart Strings Blueprint](./core.strings.blueprint.md) | [Smart Strings Foundation](./core.strings.foundation.md)
- [Vector Lane Blueprint](./core.vector_lanes.blueprint.md) | [Vector Lane Foundation](./core.vector_lanes.foundation.md)
- [Wire-Frame Blueprint](./core.wire_frame.blueprint.md) | [Wire-Frame Foundation](./core.wire_frame.foundation.md)
