# SlabFlux Compute: Timing Invariant Observer (`timing_invariant.hpp`)

## 1. Architectural Overview
In multi-threaded architectures, querying the state of the active deterministic engine from a background thread (e.g., for telemetry or health checks) typically requires locks (`std::mutex`), which instantly destroys the hot path's latency profile.

The `sovereign_observer` implements a High-Fidelity State Observer based on the Sequence Lock (SeqLock) pattern, guaranteeing lock-free, zero-stall reads for the Management plane without introducing MESI cache contention.

## 2. Hardware Geometry and False Sharing Prevention
A naive SeqLock places the atomic sequence counter directly next to the payload data. When the Reader thread polls the counter, it pulls the entire cache line into its local L1 cache. When the Writer thread updates the data, it invalidates the Reader's cache line, causing a massive interconnect stall (RFO).

The `sovereign_observer` physically isolates these components:
- The `seq_` atomic counter is padded to an `alignas(64)` boundary.
- The `data_` payload is placed on a completely separate `alignas(64)` boundary.
This ensures that read-polling the sequence marker never collides with the physical silicon currently mutating the core data payload.

## 3. Vectorized Memory Transfer
Instead of byte-by-byte copies or standard struct assignment, the observer uses `__builtin_memcpy`. 
- For fixed-size, aligned POD structures, modern compilers lower this directly into AVX-512 `_mm512_load` and `_mm512_store` instructions.
- The Writer updates the state in a single CPU cycle between sequence bumps, minimizing the "Busy" window to virtually zero.

## 4. Hardware Prefetching for Optimistic Reads
The Reader executes an optimistic lock-free read:
- Before checking the sequence counter, the Reader issues a `_mm_prefetch(..., _MM_HINT_T0)` to asynchronously pull the payload into its L1 cache.
- It reads the sequence, copies the data, and checks the sequence again.
- If the sequence is even and hasn't changed, the read is mathematically proven to be tear-free and consistent.