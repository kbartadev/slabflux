# SLABFLUX Core Technology Map: Index

## 1. Hardware Topology & System Shielding
- [Topology Docs](./topology.md) | [Topology Blueprint](./blueprint_topology.md) | [Topology Foundation](./foundation_topology.md)
- [Hardware Topology Docs](./hardware_topology.md) | [Hardware Topology Blueprint](./blueprint_topology_headers.md) | [Hardware Topology Foundation](./foundation_hardware_topology.md)
- [Cache Shield Docs](./cache_shield.md) | [Cache Shield Blueprint](./blueprint_cache_shield.md) | [Hot Path Alignment Foundation](./foundation_hot_path_alignment.md)
- [FPU Shield Docs](./fpu_shield.md) | [FPU Shield Blueprint](./blueprint_fpu_shield.md) | [FPU Shield Foundation](./foundation_fpu_shield.md)
- [TLB Warmup Docs](./tlb_warmup.md) | [TLB Warmup Blueprint](./blueprint_tlb_warmup.md) | [TLB Warmup Foundation](./foundation_tlb_warmup.md)

## 2. Memory Allocators & Pools
- [Eternal Memory Docs](./eternal_memory.md) | [Eternal Memory Blueprint](./blueprint_eternal_memory.md) | [Memory Foundation](./foundation_memory.md)
- [Validation Pool Docs](./validation_pool.md) | [Validation Pool Blueprint](./blueprint_validation_pool.md) | [Validation Pool Foundation](./foundation_validation_pool.md)
- [HugePage Allocator Docs](./hugepage_allocator.md) | [HugePage Allocator Blueprint](./blueprint_hugepage_allocator.md) | [HugePage Allocator Foundation](./foundation_hugepage_allocator.md)
- [Rank-Aware Allocator Docs](./rank_aware_allocator.md) | [Rank-Aware Allocator Blueprint](./blueprint_rank_aware_allocator.md) | [Rank-Aware Allocator Foundation](./foundation_rank_aware_allocator.md)
- [Pinned Allocator (SPSC) Docs](./pinned_allocator_spsc.md) | [Pinned Allocator Blueprint](./blueprint_pinned_allocator_spsc.md) | [Pinned Allocator (SPSC) Foundation](./foundation_pinned_allocator_spsc.md)
- [MPSC Pool Docs](./mpsc_pool.md) | [MPSC Pool Blueprint](./blueprint_mpsc_pool.md) | [MPSC Pool Foundation](./foundation_mpsc_pool.md)
- [MPMC Pool Docs](./mpmc_pool.md) | [MPMC Pool Blueprint](./blueprint_mpmc_pool.md) | [MPMC Pool Foundation](./foundation_mpmc_pool.md)

## 3. Lock-Free Conduits & Ring Buffers
- [Diffusion Conduits Docs](./diffusion_conduits.md) | [Diffusion Conduits Blueprint](./blueprint_diffusion_conduits.md) | [Conduit Foundation](./foundation_conduit.md)
- [SPSC Conduit Docs](./spsc_conduit.md) | [SPSC Conduit Blueprint](./blueprint_spsc_conduit.md) | [SPSC Conduit Foundation](./foundation_spsc_conduit.md)
- [SPSC Ring Conduit Docs](./spsc_ring_conduit.md) | [SPSC Ring Conduit Blueprint](./blueprint_spsc_ring_conduit.md) | [SPSC Ring Conduit Foundation](./foundation_spsc_ring_conduit.md)
- [MPMC Conduit Docs](./mpmc_conduit.md) | [MPMC Conduit Blueprint](./blueprint_mpmc_conduit.md) | [MPMC Conduit Foundation](./foundation_mpmc_conduit.md)
- [MPMC Sharded Conduit Docs](./mpmc_sharded_conduit.md) | [MPMC Sharded Conduit Blueprint](./blueprint_mpmc_sharded_conduit.md) | [MPMC Sharded Conduit Foundation](./foundation_mpmc_sharded_conduit.md)

## 4. Compute & Deterministic Execution
- [Vector Lane Engine Docs](./vector_lane_engine.md) | [Vector Lane Engine Blueprint](./blueprint_vector_lane_engine.md) | [Vector Lane Foundation](./foundation_vector_lane_engine.md)
- [Handler Base Docs](./handler_base.md) | [Handler Base Blueprint](./blueprint_handler_base.md) | [Pipeline Foundation](./foundation_pipeline.md)
- [Context Vault Docs](./context_vault.md) | [Context Vault Blueprint](./blueprint_context_vault.md) | [Context Vault Foundation](./foundation_context_vault.md)
- [Dispatcher Docs](./dispatcher.md) | [Dispatcher Blueprint](./blueprint_dispatcher.md) | [Dispatcher Foundation](./foundation_dispatcher.md)
- [Compute Logic Docs](./compute_logic.md) | [Compute Logic Blueprint](./blueprint_compute_logic.md) | [Compute Logic Foundation](./foundation_compute_logic.md)

## 5. Mesh & Distributed Causality
- [Causal Mesh Docs](./causal_mesh.md) | [Causal Mesh Blueprint](./blueprint_causal_mesh.md) | [LSN Engine Foundation](./foundation_lsn_engine.md)
- [Failover Orchestrator Docs](./failover_orchestrator.md) | [Failover Orchestrator Blueprint](./blueprint_failover_orchestrator.md) | [Hotpatch Bridge Foundation](./foundation_hotpatch_bridge.md)
- [Timing Wheel Docs](./timing_wheel.md) | [Timing Wheel Blueprint](./blueprint_timing_wheel.md) | [Timing Wheel Foundation](./foundation_timing_wheel.md)
- [Tagged Pointer Docs](./tagged_pointer.md) | [Tagged Pointer Blueprint](./blueprint_tagged_pointer.md) | [Tagged Pointer Foundation](./foundation_tagged_pointer.md)
- [Wire Frame LSN Docs](./wire_frame_lsn.md) | [Wire Frame LSN Blueprint](./blueprint_wire_frame_lsn.md) | [Wire Frame LSN Foundation](./foundation_wire_frame_lsn.md)

## 6. Text Representation & Serialization
- [Fixed String Docs](./fixed_string.md) | [Fixed String Blueprint](./blueprint_fixed_string.md) | [Fixed String Foundation](./foundation_fixed_string.md)
- [Smart String Docs](./smart_string.md) | [Smart String Blueprint](./blueprint_smart_string.md) | [Smart String Foundation](./foundation_smart_string.md)
- [String Service Docs](./string_service.md) | [String Service Blueprint](./blueprint_string_service.md) | [String Service Foundation](./foundation_string_service.md)
- [Trivial Serializer Docs](./trivial_serializer.md) | [Trivial Serializer Blueprint](./blueprint_trivial_serializer.md) | [Meta Foundation](./foundation_meta.md)

## 7. Telemetry & State Recovery
- [Telemetry Docs](./telemetry.md) | [Telemetry Blueprint](./blueprint_telemetry.md) | [Telemetry Foundation](./foundation_telemetry.md)
- [Snapshot Engine Docs](./snapshot.md) | [Snapshot Engine Blueprint](./blueprint_snapshot.md) | [Snapshot Foundation](./foundation_snapshot.md)
- [Replay Saga Docs](./replay_saga.md) | [Replay Saga Blueprint](./blueprint_replay_saga.md) | [Replay Saga Foundation](./foundation_replay_saga.md)
- [Integrity Watchdogs Docs](./integrity_watchdogs.md) | [Integrity Watchdogs Blueprint](./blueprint_integrity_watchdogs.md) | [Integrity Watchdogs Foundation](./foundation_integrity_watchdogs.md)
- [Autotelic Chrysalis Docs](./autotelic_chrysalis.md) | [Autotelic Chrysalis Blueprint](./blueprint_autotelic_chrysalis.md) | [Autotelic Chrysalis Foundation](./foundation_autotelic_chrysalis.md)

## 8. Network I/O
- [Fused Nexus Node Docs](./fused_nexus_node.md) | [Fused Nexus Node Blueprint](./blueprint_fused_nexus_node.md) | [Fused Nexus Node Foundation](./foundation_fused_nexus_node.md)
- [Durable Journal Docs](./durable_journal.md) | [Durable Journal Blueprint](./blueprint_durable_journal.md) | [Durable Journal Foundation](./foundation_durable_journal.md)
