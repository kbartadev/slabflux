# SlabFlux I/O: SHM Duplex (`slabflux/io/shm_duplex.hpp`)

## 1. Architectural Justification
Provides a fully bidirectional lock-free bridge over POSIX shared memory. It enables two isolated processes to function as a duplex client-server pair without invoking network stack overhead.

## 2. Hardware Implementation Directives
- **Bidirectional Ring Mapping**: Instantiates two isolated SPSC rings (one for ingress, one for egress) within the same `mmap` physical page to prevent TLB fragmentation.
- **Atomic Synchronization**: Utilizes C++ `std::atomic` operations localized entirely within the shared memory block, achieving synchronization via L1 cache coherence rather than OS semaphores.

## 3. Pipeline Integration
Integrates seamlessly into the `network_conduit` mesh. From the perspective of the `branchless_engine`, exchanging data over `shm_duplex` is indistinguishable from standard network socket duplexing, allowing trivial local-node simulation and microservice scaling.