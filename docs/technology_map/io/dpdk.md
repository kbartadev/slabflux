# SlabFlux I/O: DPDK Core (`slabflux/io/dpdk.hpp`)

## 1. Architectural Justification
Provides the foundational initialization and memory management for extreme-throughput, kernel-bypassed networking on specialized hardware (e.g., Mellanox, Intel E810). It abstracts the complexities of the DPDK framework, aligning its capabilities with the strict determinism of the SlabFlux execution core.

## 2. Hardware Implementation Directives
- **Hugepage Pre-allocation**: Bypasses standard 4KB OS pages, pre-allocating 1GB or 2MB Hugepages to guarantee contiguous physical memory and eliminate Translation Lookaside Buffer (TLB) misses during high-frequency network I/O.
- **NUMA Pinning**: Strictly enforces NUMA-aware memory allocations. All `rte_mempool` structures are bound to the specific CPU socket where the PCIe NIC resides, preventing cross-socket QPI/UPI latency penalties.
- **Environment Abstraction Layer (EAL)**: Silently manages the DPDK EAL bootstrap to mask underlying PCI device binding complexities, creating a seamless interface for higher-level ingress/egress loops.

## 3. Deterministic Pipeline Integration
The base DPDK module serves exclusively as the memory substrate. It maps the underlying physical buffers required by both `dpdk_ingress` and `dpdk_egress`, guaranteeing that once data enters the SlabFlux execution mesh, it resides entirely within cache-aligned pinned Hugepages.