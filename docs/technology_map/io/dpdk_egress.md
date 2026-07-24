# SlabFlux I/O: dpdk_egress (`slabflux/io/dpdk_egress.hpp`)

## 1. Architectural Justification
The `dpdk_egress` component governs the high-velocity transmission of Ethernet frames directly to the NIC's hardware queues. It is optimized for absolute minimum latency and microsecond-level determinism, bypassing all OS-level networking stacks.

## 2. Hardware Implementation Directives
- **Vectorized Burst Transmission**: Formats outbound data into `rte_mbuf` structures and executes `rte_eth_tx_burst` to transmit batches of network frames in a single PCIe transaction.
- **Zero-Copy Payload Handling**: Retrieves pre-allocated `rte_mbuf` structures, aligns payloads directly into pinned memory, and submits them to the DPDK Poll Mode Driver (PMD).
- **Cache-Line Aligned Descriptor Rings**: The outbound hardware descriptors are explicitly mapped into CPU cache lines to prevent False Sharing during NIC DMA fetch operations.

## 3. Deterministic Integration
Consumes outbound `wire_frame` pointers from the SlabFlux `spsc_conduit` and maps them directly to the NIC transmit queues. This ensures that the deterministic execution core is never blocked by network I/O, maintaining O(1) latency bounds for algorithmic output.
