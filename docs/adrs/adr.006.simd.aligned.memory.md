# ADR 006: SIMD-Aligned Memory Layout

## Status
Accepted

## Context
Data processing throughput is bounded by CPU cache-line efficiency and vectorization capabilities. Misaligned memory structures cause cache-line splits and prevent the compiler from generating optimal SIMD (Single Instruction, Multiple Data) instructions.

## Decision
All hot-path event structures and conduit buffers must be explicitly aligned to 64-byte boundaries (`alignas(64)`). This ensures that each object resides within its own cache line, preventing false sharing and facilitating AVX-512/AVX2 vectorization.

## Consequences
- **Positives**: Maximizes throughput by enabling SIMD-based bulk processing and eliminating cache contention across CPU cores.
- **Negatives**: Slight increase in memory footprint due to padding; requires strict adherence to alignment rules in custom event definitions.
