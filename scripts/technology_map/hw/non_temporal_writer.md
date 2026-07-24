# Blueprint: non_temporal_writer.hpp

## Architectural Overview
Provides a C++20 Concept-guarded template matrix to execute safe, branchless non-temporal writes across varying payload dimensions.

## Core Logic & Mechanisms
- **Compile-Time Width Resolution**: Statically evaluates `sizeof(T)` and utilizes `#pragma GCC unroll` to generate the exact number of 512-bit or 256-bit SIMD intrinsics required to flush the struct.
- **Cache Eviction Defense**: Uses `_mm512_stream_si512` to stream heavy mathematical output data directly to main memory, keeping the primary L1 Data cache completely pristine for routing instructions.