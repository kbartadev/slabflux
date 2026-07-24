# SlabFlux Net: NACK Handler (`nack_handler.hpp`)

## 1. Architectural Overview
In custom UDP/multicast environments, dropped packets are inevitable. The `nack_handler` listens for Negative Acknowledgement (`NACK`) requests from downstream peers and rapidly fulfills them by querying the local `retransmission_buffer`.

## 2. Hardware Cache Demotion
Serving historical frames is inherently hostile to the L1 cache, as it forces the CPU to load "stale" data that the hot-path trading matrix no longer cares about.
- When the handler retrieves a frame from the buffer, it immediately transmits it.
- Crucially, it then executes `_mm_cldemote()` (or a fallback `_MM_HINT_T1` prefetch).
- This instructs the CPU to forcefully evict the historical packet from the L1 cache and push it down to the L2/L3 cache, preserving the L1 real-estate exclusively for live, incoming market data.

## 3. Algorithmic Squelching
If a remote node suffers a network disconnect, it might spam thousands of identical NACK requests.
- The handler maintains a tracking array of the `last_sent_tsc` (Time Stamp Counter) for each LSN.
- It enforces an exponentially backing-off Squelch Timeout.
- If a duplicate NACK arrives before the squelch window expires, the handler drops the request in O(1) time without reading from the retransmission buffer, preventing DoS exhaustion.