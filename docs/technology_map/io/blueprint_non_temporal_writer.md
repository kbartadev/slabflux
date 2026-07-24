# Blueprint: non_temporal_writer.hpp

## Architectural Overview
Hardware-optimized sequential writer. Bypasses the CPU cache hierarchy using AVX-512 non-temporal instructions (`_mm512_stream_si512`) to stream logging data directly to memory controllers, preserving cache for the critical path.