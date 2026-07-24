# SlabFlux Core: Hashed Timing Wheel (`timing_wheel.hpp`)

## 1. Architectural Overview
The `timing_wheel` provides an O(1) temporal scheduling subsystem for the SlabFlux deterministic core. It completely replaces traditional tree-based timers (like `std::priority_queue` or `std::map`), which suffer from O(log N) insertion/deletion costs and severe cache fragmentation.

## 2. Hierarchical Ring Mechanics
Modeled on the Linux Kernel's timer mechanics, the wheel organizes time into cascading circular arrays representing distinct temporal resolutions (e.g., ticks, milliseconds, seconds).

### O(1) Modulo Insertion
When an event (like a TCP Retransmission timeout or an Order Cancellation) is scheduled for the future:
- The engine calculates the target hardware tick via simple modulo arithmetic against the current Sovereign Time.
- The event pointer is directly appended to the corresponding bucket array. 
- There is zero sorting involved. The CPU instruction cost is flat, regardless of whether 10 timers or 10 million timers are currently active.

### Cascading Expiry
As the engine's central `tick_event` increments the global timeline:
- The wheel advances its internal cursor.
- Any events sitting in the current bucket are instantly extracted and dispatched into the pipeline.
- If the primary ring wraps around (e.g., 256 ticks have passed), the wheel gracefully cascades elements from the higher-resolution outer rings into the primary execution ring.

## 3. Zero-Allocation Timer Nodes
Dynamic heap allocation during timer registration destroys determinism.
To combat this, the `timing_wheel` registers events by claiming pre-allocated memory wrappers from the local `spsc_pool`. 
If a timer is manually cancelled before it fires, the node is O(1) unlinked from the intrusive list and immediately recycled, preventing memory leaks or TLB saturation during high-frequency cancellation storms.

## 4. Sovereign Synchronization
The `timing_wheel` strictly obeys the `hlc_clock` (Hybrid Logical Clock) and the PTP-steered `clock_node`. It evaluates time based purely on deterministic Logical Sequence Numbers (LSNs) and disciplined RDTSC ticks, ensuring that network replays via the `replay_saga` engine fire historical timers at the exact bit-perfect sequence as they occurred in production.