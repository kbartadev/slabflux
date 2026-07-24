# SlabFlux Core: Replay Saga (`slabflux/core/replay_saga.hpp`)

## 1. Architectural Justification
A strictly deterministic engine must be able to recreate its exact internal state from scratch after a catastrophic crash. The `replay_saga` acts as the chronomancer, rapidly ingesting the asynchronous NVMe journals and feeding them back through the `branchless_engine`.

## 2. Hardware Implementation Directives
- **Non-Temporal Reads**: Ingests historical data using `_mm512_stream_load_si512` to stream gigabytes of journal state without evicting the active execution vectors from the CPU caches.
- **Sequential Prefetching**: Maps the journal files via `mmap` and issues `MADV_WILLNEED` and `_MM_HINT_NTA` to ensure the memory controller pulls data ahead of the replay loop.

## 3. Temporal Illusion
During a replay sequence, the engine is fed historical `tick_event` messages instead of real-time clocks. To the business logic, time flows exactly as it did during the original execution, perfectly preserving mathematical output hashes.