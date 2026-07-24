# SlabFlux Compute: Replay Validator (`replay_validator.hpp`)

## 1. Architectural Overview
When the `replay_manager` reconstructs the deterministic state from the `durable_journal` after a failover, the system must mathematically prove that the reconstructed state is bit-for-bit identical to the state of the active cluster. 
The `replay_validator` is a hardware-accelerated integrity checker that guarantees the perfection of the replay.

## 2. Incremental State Hashing
During live production, the `branchless_engine` periodically emits a lightweight `state_hash`.
- The `replay_validator` intercepts this and stores the `LSN -> Hash` mapping.
- During a replay sequence, as the engine ingests historical frames, it computes the exact same hardware CRC32 (`_mm_crc32_u64`) over the simulated state matrix.

## 3. Discrepancy Fencing
If the simulated hash fails to match the historical production hash at any given LSN:
- The replay halts immediately.
- The validator flags a **Divergence Panic**, indicating that either the journal was corrupted on disk or a non-deterministic artifact (like a random OS thread context) altered the execution path.
- The node refuses to transition into the `ACTIVE` state, protecting the live cluster from structural split-brain logic.

## 4. O(1) Overhead
Because the hash is computed natively in the ALU using CPU-specific CRC32 instruction sets, generating the state validation footprint introduces virtually zero overhead to the live trading or inference path.