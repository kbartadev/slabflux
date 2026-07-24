# SlabFlux I/O: http_avx (`slabflux/io/http_avx.hpp`)

## 1. Architectural Justification
The `http_avx` component transforms standard text-based parsing into a deterministic, high-velocity pipeline. It replaces conventional byte-by-byte scalar string loops with a branchless state machine powered by Advanced Vector Extensions (AVX2/AVX-512), designed to extract HTTP/1.1 or REST payload boundaries in single-digit processor cycles.

## 2. Hardware Implementation Directives
- **Vectorized Delimiter Masking**: Loads chunks of the HTTP stream (up to 32 bytes) into `__m256i` registers simultaneously. It employs `_mm256_cmpeq_epi8` to instantly locate `\r\n` (CRLF) structural boundaries across the entire vector in a single clock tick.
- **Branchless Header Extraction**: Evaluates the resulting vector mask using hardware trailing-zero counters (`_tzcnt_u32` / `__builtin_ctz`). This computes the exact physical byte-offset of headers without invoking a single conditional `if` statement, completely neutralizing branch-misprediction penalties on the CPU.
- **In-Place Matrix Slicing**: Bypasses all `std::string` or `malloc` overhead by mapping `std::string_view` spans directly over the DMA-pinned network buffer.

## 3. Algorithmic Atrial Dissection
Text-based integers (such as `Content-Length`) are converted to 64-bit numerical values in-place using register-local parallel multiplication shifts (bypassing slow ALU division blocks). The extracted payload pointer is subsequently wrapped into a `sovereign_signal` and passed down the execution manifold.