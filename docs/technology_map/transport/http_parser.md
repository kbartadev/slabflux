# SlabFlux Transport: Scalar HTTP Parser (`http.hpp`)

## 1. Architectural Overview
The `http_parser` is the foundational, strictly RFC 9110-compliant baseline parser of the SlabFlux ecosystem. It operates as a stateless (amnesiac), single-shot Deterministic Finite Automaton (DFA). 

It is designed for environments where the entire HTTP frame is guaranteed to be contiguous in memory (e.g., internal microservices, AF_XDP rings, or IPC shared memory).

## 2. Zero-Copy Execution
The parser fundamentally forbids data copying or dynamic allocation. It processes the raw network buffer character-by-character, mapping `std::string_view` projections directly onto the underlying physical memory.
- Headers are stored in a fixed-size `std::array` (max 32).
- Payload data (`body`) is merely a pointer and a length spanning the original buffer.

## 3. Strict Security Invariants
While it is a "baseline" parser, its security posture is absolute.
- **Request Smuggling Protection**: It utilizes strict boundary validation in `std::from_chars` to reject any `Content-Length` header containing trailing garbage (e.g., `Content-Length: 5 evil`), a classic vector for HTTP Desync attacks.
- **CRLF Injection Guard**: Prevents maliciously crafted URIs or Header values from terminating early and injecting spoofed headers (e.g., matching `\n` without preceding `\r` contexts).
- **Boundary-Agnostic OWS Trimming**: Safely strips Optional Whitespace (OWS) from both the leading and trailing edges of header values without triggering out-of-bounds reads.

## 4. Operational Limitations
As a single-shot parser, `http_parser` assumes the buffer contains a complete HTTP request. 
- If the buffer ends prematurely (due to TCP fragmentation), the parser returns `false`. It does not preserve state.
- It explicitly rejects `Transfer-Encoding: chunked` payloads, as chunk resolution inherently requires stateful continuation tracking.

For fragmented or hostile public internet traffic, the `baremetal_parser` must be used. For high-throughput internal routing, `http_avx_parser` provides the accelerated variant of this exact contract.