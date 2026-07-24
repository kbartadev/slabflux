# SlabFlux Net: Network Conduit (`slabflux/net/network_conduit.hpp`)

## 1. Architectural Justification
The `network_conduit` is the physical communication bus that orchestrates the transfer of state pointers across multiple execution threads. It provides cross-core message passing strictly without operating system mutexes or thread-yielding.

## 2. Hardware Implementation Directives
- **Shadow-Cached Atomic Indices**: Standard atomic indices cause intense cache-coherency traffic (L1 invalidations) between cores. The conduit maintains isolated "shadow" indices for both the producer and consumer, synchronizing the global atomic only when bounds are exhausted.
- **Wait-Free Mechanics**: It operates as a bounded ring of pointers (`void*` or typed struct addresses). Writers push pointers wait-free; readers pop pointers wait-free.
- **Cache Line Fencing**: The producer state and consumer state are strictly separated by 64-byte padding boundaries, completely eradicating False Sharing at the silicon level.

## 3. Integration in the DAG
Conduits act as the explicit edges of the execution Directed Acyclic Graph (DAG). They isolate I/O threads from the computational core, ensuring that network jitter or OS interrupts never propagate latency spikes into the deterministic pricing algorithms.