# Blueprint: shm_inline_duplex.hpp

## Architectural Overview
Optimizes small-payload (under 256 bytes) IPC data transfers by aggressively bypassing L1/L2 caches to prevent eviction of local trading logic context.

## Core Logic & Mechanisms
- **Non-Temporal Stream Siphoning**: Compresses payload properties natively into SIMD vectors (`__m512i`) and forces the write directly to the RAM bank backing the SHM segment using `_mm512_stream_si512`.
- **Zero-Copy Inlining**: Overrides the standard pointer-moving bridge pattern by performing raw byte copies of aligned structs natively into the shared matrix.