# SlabFlux Net: NACK Handler (`slabflux/net/nack_handler.hpp`)

## 1. Architectural Justification
In UDP-based deterministic environments (e.g., multicast market data), packet loss is inevitable due to switch congestion or micro-bursts. The `nack_handler` orchestrates the probabilistic detection and resubmission requests (Negative Acknowledgements) without halting or blocking the primary execution manifold.

## 2. Hardware Implementation Directives
- **Hardware Demotion**: Utilizes `_mm_cldemote` to flush historical cached frames during retransmission processing, ensuring that handling old packet drops does not pollute the L1 cache for real-time execution.
- **Squelching**: Implements sequence-locked squelch windows to prevent NACK storms. It coalesces multiple missing sequences into unified resubmission requests, maintaining deterministic throughput even during massive network drops.
- **Zero-Allocation Tracking**: Maintains missing sequence ranges in fixed-size, statically allocated circular buffers.

## 3. Pipeline Integration
Sits strictly adjacent to the inbound `server_ingress`. When the sequence validation (`wire_frame_lsn`) detects a gap in logical ordering, the flow pauses forward-progression and triggers the `nack_handler`. It pushes an asynchronous resubmission request to the egress ring, waiting for the retransmitted frame to fill the gap before resuming execution.