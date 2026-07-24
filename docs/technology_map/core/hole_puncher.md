# SlabFlux Core: Hole Puncher (`hole_puncher.hpp`)

## 1. Architectural Overview
In distributed clustering, the `causal_sequencer` guarantees that events are processed in strict logical order (LSN 1, 2, 3...). Due to network jitter or redundant UDP paths, packets frequently arrive out of order (e.g., LSN 1, 3, 4). 
The `hole_puncher` is an O(1) wait-free staging buffer that temporarily quarantines "future" events until the missing gaps are filled, preventing timeline corruption without stalling the ingress thread.

## 2. O(1) Sparse Array Quarantine
The `hole_puncher` operates as a localized, bitmask-backed circular array.
- When LSN 3 arrives, the engine calculates its offset relative to the expected LSN 2 (`offset = 3 - 2 = 1`).
- The event is written directly to index `1` in the circular buffer, and the corresponding bit in a 64-bit `presence_mask` is flipped to `1`.
- Because it relies on direct indexing, out-of-order insertions are resolved in constant time, utterly avoiding the `O(log N)` penalty of standard `std::map` or priority queues.

## 3. Instant Cascade Unlocking
When the missing packet (LSN 2) finally arrives (either naturally delayed or via the `nack_handler` retransmission):
1. The engine processes LSN 2.
2. The `hole_puncher` executes a hardware bit-scan (`__builtin_ctzll`) on the `presence_mask` to instantly locate the next contiguous block of quarantined events.
3. It rapidly streams LSN 3 and LSN 4 out of the buffer and into the deterministic pipeline, seamlessly repairing the causal timeline in a microsecond burst.