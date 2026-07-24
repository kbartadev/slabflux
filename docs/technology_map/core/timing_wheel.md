# SlabFlux Core: Hashed Timing Wheel (`timing_wheel.hpp`)

## 1. Architectural Overview
Replaces O(log N) tree-based timers (`std::map`, `std::priority_queue`) with an O(1) cascading temporal ring, enabling sub-microsecond timer resolution without heap allocations.

## 2. O(1) Modulo Insertion
Target hardware ticks are calculated via simple modulo arithmetic and appended to pre-allocated resolution buckets.
- Timer registration claims pre-allocated wrappers from a local `spsc_pool`.
- Cancelled timers are instantly unlinked and recycled, preventing TLB saturation and avoiding pointer chasing.

## 3. Cascade Mechanics
When the lowest resolution ring (e.g., microseconds) completes a full revolution, the wheel instantly cascades timers down from the next tier (e.g., milliseconds) into the current execution slots.