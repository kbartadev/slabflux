# Blueprint: http_avx.hpp

## Architectural Overview
The `http_avx.hpp` component is an ultra-fast, zero-allocation HTTP/1.1 parsing module designed exclusively for HFT API gateways. It bypasses character-by-character scalar parsing using vectorized hardware instructions.

## Core Logic & Mechanisms
- **SIMD Chunk Processing**: Loads incoming memory via `_mm256_loadu_si256` to inspect 32 bytes of the payload in a single clock cycle. It utilizes `_mm256_cmpeq_epi8` to instantly locate structural delimiters (Spaces, Colons, Newlines).
- **Hardware Bitmask Routing**: Leverages `_mm256_movemask_epi8` combined with Trailing Zero Count (`tzcnt_32`) hardware intrinsics to calculate string lengths and boundaries without executing standard library loops.
- **Zero-Copy Projection**: Maps `std::string_view` windows directly over the pre-allocated incoming network ring buffer (`raw_buffer`). It completely prohibits heap-allocated `std::string` copies.
- **Determinism Gate**: Actively drops non-deterministic HTTP features (e.g., `Transfer-Encoding: chunked`) to protect the execution latency from arbitrary payload accumulation stalls.