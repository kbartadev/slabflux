# Blueprint: Hashed Timing Wheels

## Architectural Overview
Provides an O(1) temporal scheduling subsystem, completely replacing tree-based `std::priority_queue` timers. This allows the engine to schedule millions of future events (e.g., timeouts, re-transmissions) without sorting penalties.

## Core Logic & Mechanisms
- **Hierarchical Ring Arrays**: Organizes time into cascading circular arrays representing distinct temporal resolutions (e.g., milliseconds, seconds, minutes).
- **O(1) Insertion & Expiry**: Timers are inserted via pure modulo arithmetic based on the target execution tick. As the hardware TSC advances the global engine clock, the current bucket is executed instantly without traversing upcoming events.
- **Zero-Allocation Nodes**: Timer registration structures are pre-allocated within the underlying `spsc_pool`, allowing temporal bounds to be tracked without invoking the heap allocator.