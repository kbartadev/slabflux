# SlabFlux Net: TCP Stream Defragmenter (`slabflux/net/tcp_stream_defragmenter.hpp`)

## 1. Architectural Justification
Unlike UDP datagrams, TCP arrives as an unstructured, fragmented byte stream. The `tcp_stream_defragmenter` is a zero-allocation sliding-window engine that scans overlapping, chaotic network segments and reconstructs logical application frames cleanly.

## 2. Hardware Implementation Directives
- **SIMD Stream Scanning**: Rather than iterating byte-by-byte, the defragmenter loads 32-byte vectors of the incoming stream into AVX2 registers. It uses hardware masking (`_mm256_cmpeq_epi8`) to search for expected structural delimiters in a single CPU cycle.
- **In-Place Reassembly**: If a logical frame spans two TCP packets, the engine does not copy the tail into a new `std::string`. Instead, it maintains mapping vectors against the pinned DMA memory, orchestrating a transparent continuous view for the upstream parser.

## 3. Execution Handoff
Once a structurally valid frame is verified, the defragmenter updates the boundary indices and instantly pushes the resulting memory span as an atomic event down the deterministic conduit, isolating the compute core from TCP windowing mechanics.