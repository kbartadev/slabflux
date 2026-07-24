# SlabFlux Core: SPSC Conduit (`spsc_conduit.hpp`)

## 1. Architectural Overview
The `spsc_conduit` (Single-Producer Single-Consumer) is the ultimate zero-contention, lock-free ring buffer. It is designed specifically for thread-to-thread boundary crossings where only exactly one thread writes and exactly one thread reads, eliminating the need for expensive Compare-And-Swap (CAS) operations entirely.

## 2. Zero-Contention Geometry
In standard queues, the `head` and `tail` pointers reside on the same 64-byte cache line. When Thread A pushes and Thread B pops, they violently invalidate each other's L1 cache, causing massive interconnect latency (False Sharing).
- The `spsc_conduit` physically segregates the `head` and `tail` indices.
- They are padded with `alignas(std::hardware_constructive_interference_size)` (64 bytes).
- The producer and consumer update their local cache lines independently, syncing only via relaxed atomic memory barriers.

## 3. Power-of-Two Masking
To wrap the cursor around the ring buffer, the conduit enforces `RequestedSize` to be a power of two. This replaces the slow integer division instruction (`DIV`) with a single-cycle bitwise AND mask (`cursor & (RequestedSize - 1)`), preserving the execution cycle budget.

## 4. Saturation and Aphasic Fencing
If the consumer (e.g., the Compute Engine) stalls and the producer (e.g., Network Ingress) fills the queue, the `spsc_conduit` does not block.
- It intercepts the overflow event using `on_conduit_full_drop()`.
- It triggers the **Aphasic Horizon** via `bind_aphasic_horizon()`.
- This elegantly handles backpressure by routing the dropped packet into the Teleological Agnosia sinkhole, ensuring the Network NIC is never starved or blocked by a downstream logic delay.