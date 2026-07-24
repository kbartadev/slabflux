# SlabFlux Core: Round-Robin Switch (`round_robin_switch.hpp`)

## 1. Architectural Overview
The `round_robin_switch` is a highly optimized Fan-Out routing node. It deterministically shards high-frequency event streams (like market data ticks or AI stimuli) across a parallel array of downstream worker threads (e.g., `vector_lane_engine` instances) to achieve linear scaling.

## 2. O(1) Lock-Free Sharding
Standard load balancers utilize atomic counters or mutexes to determine the next available worker, introducing massive interconnect contention.

The `round_robin_switch` abandons shared states:
- It maintains a thread-local, non-atomic `cursor_` initialized to 0.
- When an event arrives, the switch routes the pointer into the downstream `spsc_conduit` corresponding to the `cursor_`.
- It increments the cursor using a fast bitwise modulo operation (`cursor_ = (cursor_ + 1) & (NumWorkers - 1)`), exploiting power-of-two capacities to avoid slow integer division (`DIV` instructions).

## 3. Backpressure Absorption
If a specific downstream worker becomes congested (e.g., due to an OS jitter spike), its ingress conduit will saturate.
- The switch executes a non-blocking `try_push`.
- If the target conduit is full, the switch does not spin-wait (which would stall the global ingress). 
- Instead, it instantly advances the cursor and attempts to route the event to the next available worker, naturally bypassing localized CPU stalling and maintaining maximum line-rate throughput.

## 4. Vectorized Scatter
When paired with the `pipeline_lane` and `managed_batch` systems, the `round_robin_switch` supports vectorized scatter operations. It can ingest a block of 32 network events and distribute them in chunks of 4 directly into the downstream queues, keeping AVX-512 pipelines optimally fed without scalar bottlenecking.