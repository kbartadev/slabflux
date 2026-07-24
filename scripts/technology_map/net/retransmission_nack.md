# SlabFlux Net: Retransmission Buffer & NACK Handler

## 1. Architectural Overview
The `retransmission_buffer` provides a high-performance, O(1) sliding window cache for Logical Sequence Number (LSN) frames. It temporarily holds recently broadcasted payloads to fulfill Negative Acknowledgment (NACK) requests parsed by the `nack_handler`.

## 2. Seqlock-Protected Sliding Window
The buffer avoids heavy mutexes by using a Sequence Lock (Seqlock) pattern:
- Before writing, the producer marks the slot's atomic LSN as a `BUSY_SENTINEL`.
- Once the frame is copied, it atomically publishes the true LSN with `std::memory_order_release`.
- This ensures that asynchronous NACK threads never read a partially overwritten, torn frame.

## 3. Accelerated Burst Extraction
During a NACK storm, the buffer uses software-pipelined bulk extraction:
- It iterates over the requested LSNs and executes `_mm_prefetch(..., _MM_HINT_T0)` two iterations ahead to hide DRAM latency.

## 4. Probabilistic Squelching & Cache Demotion
The `nack_handler` tracks the `last_sent_tsc` for every LSN. The squelch timeout dynamically scales based on the LSN distance from the current horizon. To preserve L1 cache residency for the trading logic, the handler executes `_mm_cldemote()` after transmitting a frame, natively forcing the CPU to push the buffered frame down to the L2/L3 cache.