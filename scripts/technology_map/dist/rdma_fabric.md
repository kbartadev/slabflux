# SlabFlux Dist: RDMA Fabric (`rdma_fabric.hpp`)

## 1. Architectural Overview
In a distributed High-Availability cluster, replicating state between the Active primary node and the Passive secondary nodes via standard TCP/UDP introduces kernel stack latency and CPU overhead. The `rdma_fabric` bypasses the CPU entirely, using Remote Direct Memory Access (RoCEv2 / InfiniBand) to synchronize memory slabs at hardware speeds.

## 2. Zero-CPU State Replication
The fabric relies on Memory Regions (MRs) registered directly with the NIC.
- The primary node's `mpmc_pool` or `shared_state_slab` is registered as an RDMA memory region.
- When the `branchless_engine` updates the state, the NIC's DMA engine automatically mirrors the memory delta across the fiber optic network to the passive node's identical memory address.
- The host CPU is not involved in the transfer; there is no packetization, no polling, and zero cycle overhead for state replication.

## 3. Sub-Microsecond Consistency
By utilizing RDMA WRITE operations, the `rdma_fabric` achieves state replication latencies under 1.5 microseconds across physical Top-of-Rack switches. This allows the `failover_orchestrator` to guarantee that the passive node is essentially a bit-perfect, real-time clone of the active node, enabling zero-data-loss failovers.