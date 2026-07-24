# SlabFlux I/O: dpdk_ingress (`slabflux/io/dpdk_ingress.hpp`)

## 1. Architectural Justification
The `dpdk_ingress` component handles the high-velocity ingestion of Ethernet frames directly from the NIC's hardware queues. It is optimized for environments where microsecond-level determinism is more critical than CPU power efficiency.

## 2. Hardware Implementation Directives
- **Vectorized Burst Reception**: Uses `rte_eth_rx_burst` to harvest multiple packets in a single PCIe transaction. The engine processes incoming packets in optimal batches (e.g., 32 frames) to maximize L1 instruction cache efficiency.
- **Zero-Copy Payload Slicing**: Bypasses all `sk_buff` allocations. The ingress engine extracts the raw payload pointer from the `rte_mbuf` struct, executes SIMD protocol validation, and wraps it in a C++ reference for the deterministic engine.
- **Software Prefetching**: Actively applies `_mm_prefetch(..., _MM_HINT_T0)` to incoming `rte_mbuf` headers ahead of the parsing loop, ensuring the CPU's memory controllers pull the Ethernet headers into the L1 cache before the ALU attempts to read them.

## 3. Deterministic Handoff
Validated packet pointers are injected into the SlabFlux `spsc_conduit`. The deterministic execution thread consumes these pointers, meaning the heavy lifting of network traversal is fully isolated from the trading or business logic.
