# SlabFlux Net: Retransmission Buffer (`retransmission_buffer.hpp`)

## 1. Architectural Overview
In custom UDP multicast trading architectures, mitigating dropped packets (NACK handling) is critical. Traditional applications cache outbound messages on the heap, forcing expensive garbage collection. 
The `retransmission_buffer` is a zero-allocation, sliding-window cache that tracks historical Logical Sequence Numbers (LSNs) to instantly fulfill network retransmission requests.

## 2. Lock-Free Seqlock Concurrency
To avoid using a global mutex while the egress thread actively writes and the NACK-recovery thread actively reads, the buffer utilizes a SeqLock (Sequence Lock) pattern:
- Every slot maintains a `published_lsn` atomic marker.
- Before the producer overwrites a slot, it marks it with a `BUSY_SENTINEL` (0xFFFFFFFFFFFFFFFE) using `memory_order_release`.
- The consumer (reader) loads the marker before and after reading the payload. If the marker matches the requested LSN and didn't transition to BUSY during the read, the extraction is mathematically safe and free from torn reads.

## 3. Accelerated Burst Extraction
When a downstream matching engine misses a large sequence of packets (e.g., dropping LSNs 1000 to 1050), it blasts a bulk NACK back to the server.

To handle this without stalling the egress loop:
- The buffer executes `extract_burst`.
- It employs **Software Pipelining**: it issues an `_mm_prefetch(..., _MM_HINT_T0)` two iterations ahead of the active loop. By the time the CPU attempts to extract LSN `N+2`, the physical cache line has already been pulled from DRAM into the L1 cache, entirely masking the latency of non-sequential memory lookups.