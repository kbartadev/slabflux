# SlabFlux I/O: Buffer Flush (`slabflux/io/buffer_flush.hpp`)

## 1. Architectural Justification
Hardware memory consistency guard. In cross-core and core-to-device communications, ensuring that memory writes hit global visibility in the correct order is paramount.

## 2. Hardware Implementation Directives
- **Cache-Line Eviction**: Uses `_mm_clwb` (Cache Line Write Back) to precisely flush modified cache lines containing deterministic state out to main memory.
- **Store Fencing**: Enforces `_mm_sfence` to guarantee that all payload data is globally visible before the descriptor ring's tail pointer is updated.
- **Zero Overhead Integration**: Implemented purely as intrinsic compiler directives, executing in a few hardware cycles directly within the transmission loops.