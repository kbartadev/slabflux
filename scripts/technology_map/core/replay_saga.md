# SlabFlux Core: Replay Saga (`replay_saga.hpp`)

## 1. Architectural Overview
The `replay_saga` is a deterministic state reconstruction engine. It processes a durable NVMe log file to recreate the exact bit-perfect execution state of the `branchless_engine`, enabling identical replicas for crash recovery, failover, or forensic analysis.

## 2. Zero-Syscall File Ingestion
Traditional replay mechanisms utilize sequential `read()` syscalls in an `O(N)` loop, which completely destroys deterministic replay speed. 
The `replay_saga` implements a single, zero-copy `mmap` spanning the entire file size with `MAP_POPULATE` and `MAP_SHARED`. This pre-faults the memory and eliminates Page Fault micro-stalls during the replay sequence.

## 3. Bit-Perfect State Reconstruction
Because the SlabFlux architecture executes deterministically without OS interrupts or locks on the hot path:
- Replaying the historical `LSN`s directly into the scalar/SIMD `vector_lane_engine` reproduces the exact register states.
- The LSN commits are sequentially advanced locally, tricking the environment into structurally validating the historical sequence as the current live sequence without modification.

## 4. Integration with Replay Manager
When combined with the `replay_manager`, the Saga acts as the "Cold Path" boot step. Once the LSN aligns with the journal tail, the system automatically proceeds to "Prime the Silicon" by training the Branch Target Buffer (BTB) with dummy events before seamlessly unlocking the network ingress.