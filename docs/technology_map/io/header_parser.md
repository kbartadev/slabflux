# SlabFlux I/O: Header Parser (`slabflux/io/header_parser.hpp`)

## 1. Architectural Justification
Extracts fundamental protocol Type IDs and sequence boundaries from raw network streams. Must operate in absolute O(1) time to ensure predictable deterministic latency.

## 2. Hardware Implementation Directives
- **Deterministic Dispatch**: Replaces `if/else` ladders with O(1) jump-tables or hardware-aligned lookup arrays.
- **In-Place Validation**: Reads protocol fields directly from the DMA-pinned network buffer. Casts `alignas(64)` struct pointers over the memory to avoid copying into localized cache blocks.
- **Cache Pre-Warming**: Works concurrently with `_mm_prefetch` directives emitted by the ingress loop, meaning the memory is already residing in the L1 cache when the jump-table logic is executed.

## 3. Pipeline Integration
The header parser acts as the initial classification gate before deeper structural evaluation. Once the Type ID is identified, it tags the `sovereign_signal` envelope and forwards it down the SPSC conduit to the correct typed SAGA or strategy handler without invoking virtual function dispatch.