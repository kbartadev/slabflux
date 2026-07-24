# SlabFlux I/O: Endian Math (`slabflux/io/endian.hpp`)

## 1. Architectural Justification
High-frequency protocol parsing requires constant conversions between Network Byte Order (Big Endian) and Host Byte Order (Little Endian). Standard library `ntohl()` functions lack vectorized scalability.

## 2. Hardware Implementation Directives
- **BSWAP Intrinsics**: Utilizes single-cycle `__builtin_bswap64` and `__builtin_bswap32` directly against CPU registers.
- **SIMD Shuffling**: For bulk conversions, applies AVX2 `_mm256_shuffle_epi8` with pre-computed mask registers to byte-swap 32-byte blocks in a single ALU tick.
- **Zero-Branch Math**: The functions are `constexpr` evaluated to inject hardware-direct byte manipulators inline without executing conditional checks.

## 3. Integration in the DAG
Embedded as inline utilities across all transport and protocol modules. Used natively during serialization within `wire_protocol` and inbound header extraction within `structural_parser` to guarantee memory writes and reads remain O(1).