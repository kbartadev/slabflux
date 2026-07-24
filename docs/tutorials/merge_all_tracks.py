#!/usr/bin/env python3
import os
import shutil
from pathlib import Path

"""
SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
SLABFLUX UNIFIED TRACK MERGER (Root + Off + Regi)

This script flattens the tutorial directory by merging 'off/', 'regi/', 
and existing root 'tut.*' files into a single deterministic sequence.
"""

BASE_DIR = Path("/home/kris/src/base/slabflux_rte/docs/tutorials")
OFF_DIR = BASE_DIR / "off"
REGI_DIR = BASE_DIR / "regi"
TEMP_DIR = BASE_DIR / "temp_merged"

# The Master Plan: Interweaving all three sources into one logical flow.
# Format: "Target Filename": (Source Directory, Original Filename)
MASTER_PLAN = {
    # --- PHASE 1: FOUNDATIONS ---
    "tut.1.0.sovereign_core_philosophy.md": (OFF_DIR, "tut.01.sovereign.core.md"),
    "tut.1.1.memory_leasing.md": (BASE_DIR, "tut.1.1.memory_leasing.md"),
    "tut.1.2.deterministic_dispatch.md": (BASE_DIR, "tut.1.2.deterministic_dispatch.md"),
    "tut.1.3.advanced_routing.md": (BASE_DIR, "tut.1.3.advanced_routing.md"),
    "tut.1.4.deterministic_unrolling.md": (OFF_DIR, "tut.02.deterministic.unrolling.md"),
    "tut.1.5.context_vault_geometry.md": (OFF_DIR, "tut.03.memory.vaults.md"),
    "tut.1.6.memory_pool_practice.md": (REGI_DIR, "tut.02.memory.pool.in.practice.md"),
    "tut.1.7.lockfree_conduit_topologies.md": (OFF_DIR, "tut.04.lockfree.conduits.md"),
    "tut.1.8.atomic_state_transitions.md": (REGI_DIR, "tut.05.lockfree.concurrency.md"),

    # --- PHASE 2: ORCHESTRATION ---
    "tut.2.1.sovereign_ignition.md": (BASE_DIR, "tut.2.1.sovereign_ignition.md"),
    "tut.2.2.event_arbitration.md": (BASE_DIR, "tut.2.2.event_arbitration.md"),
    "tut.2.3.failover_orchestrator.md": (BASE_DIR, "tut.2.3.failover_orchestrator.md"),
    "tut.2.4.execution_halts_theory.md": (OFF_DIR, "tut.07.execution.halts.md"),
    "tut.2.5.graceful_drain_practice.md": (REGI_DIR, "tut.07.graceful.drain.md"),
    "tut.2.6.static_demux_logic.md": (REGI_DIR, "tut.20.compile.time.demuxing.md"),

    # --- PHASE 3: COMPUTE ---
    "tut.3.1.vector_lanes.md": (BASE_DIR, "tut.3.1.vector_lanes.md"),
    "tut.3.2.cognitive_inference.md": (BASE_DIR, "tut.3.2.cognitive_inference.md"),
    "tut.3.3.avx512_saturation.md": (REGI_DIR, "tut.10.simd.compute.md"),
    "tut.3.4.snapshot_engines_theory.md": (OFF_DIR, "tut.08.snapshot.engines.md"),

    # --- PHASE 4: HARDWARE ---
    "tut.4.1.ssds_offload.md": (BASE_DIR, "tut.4.1.ssds_offload.md"),
    "tut.4.2.signal_multiplexing.md": (BASE_DIR, "tut.4.2.signal_multiplexing.md"),
    "tut.4.3.hardware_isomorphism.md": (OFF_DIR, "tut.05.hardware.isomorphism.md"),
    "tut.4.4.numa_affinity_laws.md": (REGI_DIR, "tut.09.hardware.topology.md"),

    # --- PHASE 5: I/O & NETWORKING ---
    "tut.5.1.zero_syscall_bypass.md": (BASE_DIR, "tut.5.1.zero_syscall_bypass.md"),
    "tut.5.2.durable_journaling.md": (BASE_DIR, "tut.5.2.durable_journaling.md"),
    "tut.5.3.zero_blocking_io_theory.md": (OFF_DIR, "tut.11.zero.blocking.io.md"),
    "tut.5.4.io_uring_practice.md": (REGI_DIR, "tut.11.zero.blocking.io.md"),

    # --- PHASE 6: RESILIENCE ---
    "tut.6.1.chaos_engine.md": (BASE_DIR, "tut.6.1.chaos_engine.md"),
    "tut.6.2.hole_puncher.md": (BASE_DIR, "tut.6.2.hole_puncher.md"),
    "tut.6.3.cache_partitioner.md": (BASE_DIR, "tut.6.3.cache_partitioner.md"),
    "tut.6.4.mpmc_mesh_contention.md": (OFF_DIR, "tut.09.mpmc.conduits.md"),
    "tut.6.5.jitter_injection_practice.md": (REGI_DIR, "tut.16.chaos.engineering.md"),

    # --- PHASE 7: GATEWAYS ---
    "tut.7.1.baremetal_parser.md": (BASE_DIR, "tut.7.1.baremetal_parser.md"),
    "tut.7.2.chicago_gateway.md": (BASE_DIR, "tut.7.2.chicago_gateway.md"),
    "tut.7.3.physics_reactor.md": (BASE_DIR, "tut.7.3.physics_reactor.md"),
    "tut.7.4.zero_copy_net_theory.md": (OFF_DIR, "tut.06.zero.copy.net.md"),
    "tut.7.5.network_gateway_practice.md": (REGI_DIR, "tut.14.network.gateway.md"),

    # --- PHASE 8: TIMELINE ---
    "tut.8.1.timeline_management.md": (BASE_DIR, "tut.8.1.timeline_management.md"),
    "tut.8.2.deterministic_strings.md": (BASE_DIR, "tut.8.2.deterministic_strings.md"),
    "tut.8.3.distributed_determinism_theory.md": (OFF_DIR, "tut.10.distributed.determinism.md"),
    "tut.8.4.deterministic_replay_practice.md": (REGI_DIR, "tut.15.deterministic.replay.md"),
    "tut.8.5.sub_nanosecond_timekeeping.md": (REGI_DIR, "tut.13.sub.nanosecond.timekeeping.md")
}

def run_unified_merge():
    print("Starting Unified SlabFlux Tutorial Merge (Root + Off + Regi)...")
    
    # Create temp directory to avoid moving files into themselves
    if TEMP_DIR.exists():
        shutil.rmtree(TEMP_DIR)
    TEMP_DIR.mkdir()

    count = 0
    for target_name, (source_dir, original_name) in MASTER_PLAN.items():
        src_path = source_dir / original_name
        dest_path = TEMP_DIR / target_name
        
        if src_path.exists():
            print(f"  [+] Staging: {src_path.relative_to(BASE_DIR.parent)} -> {target_name}")
            shutil.copy2(src_path, dest_path)
            count += 1
        else:
            print(f"  [-] Missing: {src_path.relative_to(BASE_DIR.parent)}")

    # Atomic swap: Cleanup root tut files and move staged ones in
    print("\nFinalizing root directory...")
    for root_tut in BASE_DIR.glob("tut.*.md"):
        root_tut.unlink()
    
    for staged_file in TEMP_DIR.glob("*.md"):
        shutil.move(str(staged_file), str(BASE_DIR / staged_file.name))

    # Cleanup
    shutil.rmtree(TEMP_DIR)
    
    print(f"\nSuccess. {count} tutorials merged into {BASE_DIR}")
    print("The 'off/' and 'regi/' folders are now safe to remove.")

if __name__ == "__main__":
    run_unified_merge()