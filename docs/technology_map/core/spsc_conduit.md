# SlabFlux Core: SPSC Conduit (`spsc_conduit.hpp`)

## 1. Architectural Overview
The Single-Producer Single-Consumer conduit is the ultimate zero-contention, lock-free ring buffer. It is designed to pass memory ownership across thread boundaries at bare-metal speeds, operating strictly on raw pointer transmission.

## 2. Zero-Contention Geometry
Traditional ring buffers suffer from False Sharing due to the `head` and `tail` atomic cursors residing on the same cache line.
- In `spsc_conduit`, the `head` and `tail` cursors are physically separated by `alignas(64)` padding (mapping to `std::hardware_constructive_interference_size`).
- This eliminates MESI protocol invalidation storms (RFO stalls) when the producer and consumer threads run on different physical cores.

## 3. Power-of-Two Masking
The conduit capacity must be explicitly defined as a power of two at compile time (e.g., `Capacity = 65536`).
- This replaces the expensive modulo (`DIV`) instruction with a single-cycle bitwise `AND` mask (`index & (Capacity - 1)`).
- Ensures that cursor wrapping logic introduces zero branching or pipeline stalls.

## 4. Relaxed Memory Semantics & Backpressure
- Updates to physical index positions are driven via `std::memory_order_relaxed` internally.
- Synchronizing `acquire` and `release` fences are used strictly at the structural boundary, avoiding global sequential consistency fences.
- **Backpressure**: The `try_push()` operation rejects pointer ingestion directly in O(1) latency when capacity matches the opposing read counter.
- This allows the upstream pipeline to instantly route dropped packets to an Aphasic Horizon or Teleological Agnosia sinkhole rather than blocking execution ports.