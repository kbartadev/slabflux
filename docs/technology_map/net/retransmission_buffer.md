# SlabFlux Net: Retransmission Buffer (`slabflux/net/retransmission_buffer.hpp`)

## 1. Architectural Justification
In unreliable multicast or custom UDP deployments, packet drops are inevitable. The `retransmission_buffer` serves as an O(1) sliding window cache designed to archive outbound frames dynamically, enabling instantaneous state recovery.

## 2. Hardware Implementation Directives
- **Garbage-Collection-Free Eviction**: The buffer utilizes a modulus-bound circular array. As new outbound frames are generated, the oldest frames are naturally overwritten. Memory is never dynamically allocated or explicitly freed.
- **Cache Demotion Integration**: When fulfilling a NACK (Negative Acknowledgement), the buffer retrieves historical pointers. It couples with hardware cache-demotion intrinsics (`_mm_cldemote`) to ensure serving old data does not pollute the L1 compute cache.

## 3. Zero-Copy Resubmission
Because outbound payloads are immutable, the buffer only stores physical memory pointers. During a NACK response, the buffer simply re-links the historical pointer directly to the NIC egress ring, retransmitting the exact same bytes without any serialization logic.