# SlabFlux Core: MPMC Conduit (`mpmc_conduit.hpp`)

## 1. Architectural Overview
The `mpmc_conduit` implements a high-throughput, lock-free concurrent ring buffer utilizing a **Sequence-Validated Matrix** architecture. It provides wait-free, O(1) transitions for both producers and consumers in highly contested multi-threading topologies.

## 2. Detached Matrix Architecture
To prevent False Sharing, the conduit separates the atomic sequence metadata from the payload data.
- The metadata array and the data array reside on different cache lines.
- Head and tail markers (Ingress/Egress gates) are padded to 64-byte boundaries, eliminating MESI thrashing (Read-For-Ownership stalls).

## 3. Hardware-Managed Residency
The conduit utilizes `mmap` with `MAP_HUGETLB | MAP_HUGE_2MB` and `mlock` to guarantee physical RAM residency. This eliminates TLB misses and prevents the OS from ever swapping the lock-free queues to disk.

## 4. Sharded Contention Lanes
Instead of a single bottleneck, the conduit dynamically shards traffic across multiple parallel lanes (`NumLanes`). Threads dynamically select their lane using sticky routing based on `hardware_topology::get_current_cpu()`, ensuring that operations remain on the physically local memory channel and avoiding cross-socket QPI latency.