# SlabFlux Transport: HTTP AVX Parser (`http_avx.hpp`)

## 1. Architectural Overview
Standard HTTP parsers process byte streams linearly, using scalar branch loops (`if (c == '\n')`) that destroy CPU branch-predictor performance. In high-frequency gateways, this parsing overhead introduces unacceptable micro-jitter.

The `http_avx_parser` evaluates HTTP REST/WebSocket payloads using pure hardware silicon (AVX2/AVX-512), completely flattening the parsing tree and achieving parsing latencies measured in nanoseconds.

## 2. Hardware-Accelerated ASCII Processing
The parser relies on 256-bit wide hardware vector instructions to ingest chunks of network payload:
- **O(1) Whitespace/Newline Resolution**: It loads 32 characters of the HTTP request into a `__m256i` register. Using `_mm256_cmpeq_epi8`, it instantly identifies the position of spaces and newlines across the entire block.
- **Hardware Trailing Zeros (`tzcnt`)**: The resulting byte mask is fed directly into the CPU's Trailing Zero Count hardware, identifying the exact string lengths without iterating.

## 3. Zero-Copy String Views
The `http_request` struct fundamentally avoids `std::string` heap allocations.
- Every header and path variable is natively mapped to a `std::string_view`.
- The parser simply maps these views directly over the raw, pre-aligned `raw_buffer` injected from the `demux_gateway`.

## 4. Algorithmic Security Constraints
To prevent deterministic execution timeouts caused by malicious actors (e.g., HTTP DoS attacks):
- **Chunked Encoding Rejection**: The parser executes a branchless case-insensitive comparison (`iequals`) against "Transfer-Encoding". If chunked data is detected, it terminates parsing instantly, preserving the compute budget.
- **Hard Bounds**: Headers are strictly capped (`MAX_HEADERS = 16`), and payloads are constrained to cache-aligned 1024-byte chunks, eradicating Buffer Overflow and CPU saturation vectors.