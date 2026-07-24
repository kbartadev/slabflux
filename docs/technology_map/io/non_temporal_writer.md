# SlabFlux I/O: Non-Temporal Writer (`slabflux/io/non_temporal_writer.hpp`)

## 1. Architectural Justification
Writes large sequential streams of data (such as network journals or PCAP captures) without polluting the L1/L2/L3 CPU caches, preserving cache space for the deterministic execution algorithms.

## 2. Hardware Implementation Directives
- **Streaming Stores**: Employs AVX-512 non-temporal instructions (`_mm512_stream_si512`) to bypass the cache hierarchy completely. Data is pushed directly to the memory controllers/Write-Combining buffers.
- **Write-Combining**: Batches 64-byte writes internally within the CPU's Write-Combining (WC) buffers before executing a burst write across the memory bus.
- **Memory Barrier Sync**: Applies `_mm_sfence` explicitly after bursts to serialize the transactions cleanly over the PCI bus.

## 3. Handoff & Asynchronous Logging
Primarily utilized by the `mirrored_journal` and PCAP recording pipelines. By writing outbound state records directly to memory via non-temporal streams, it ensures that high-volume disk-flushing operations do not thrash the execution core's L1/L2 caches.