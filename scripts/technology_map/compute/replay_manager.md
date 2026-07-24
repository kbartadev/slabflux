# SlabFlux Compute: Replay Manager (`replay_manager.hpp`)

## 1. Architectural Overview
In the event of a cluster failover or application crash, the secondary node must identically reconstruct the state of the failed node before resuming live network transmission. 
The `replay_manager` acts as the deterministic gap-fill mechanism, sweeping historical Journal records directly into the SIMD execution mesh.

## 2. Monotonic Replay Injection
The manager initializes the `REPLAYING` state:
- It streams events from the `durable_journal` via NVMe `pread` or `mmap`.
- It strictly interrogates the LSN of each historical event. If the event's `sequence_id` is less than or equal to the engine's current state LSN, the event is harmlessly dropped.
- Valid events are injected natively into the `branchless_engine::process_event()` interface. This guarantees that the replayed events follow the exact same mathematical trajectory as live ingress traffic.

## 3. Physical Silicon Priming
Once the state is perfectly reconstructed, switching instantly to live network traffic is dangerous: the CPU's Branch Target Buffer (BTB) is cold, and the Instruction Cache (I-Cache) has been evicted by the journal reading routines, leading to a massive latency spike on the first live packet.

The `prime_silicon()` stage mitigates this:
- It shifts the engine to `WARMING` state.
- It generates 10,000 "Ghost Events"—empty events that traverse the entire execution pipeline without mutating the actual data matrix.
- It forces a false hardware dependency (`asm volatile("" : "+r,m"(lto_defeat_counter) : : "memory")`) to prevent the C++ compiler from optimizing the loop away.
- This aggressively warms up the branch predictors, preloads the L1/L2 caches, and locks the CPU into maximum Turbo frequency (C0 state) right before the system flips back to `LIVE`.