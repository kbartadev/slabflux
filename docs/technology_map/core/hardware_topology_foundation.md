# Foundation: Hardware Topology (`slabflux/core/hardware_topology.hpp`)

## 1. Architectural Justification
Relying on the Linux Completely Fair Scheduler (CFS) ensures that threads will be migrated across cores, instantly destroying L1/L2 cache locality and forcing data across the high-latency QPI/Infinity Fabric. The `hardware_topology` maps and locks threads physically to silicon dies.

## 2. Hardware Implementation Directives
- **NUMA Discovery**: Scans `/sys/devices/system/node` during the `ignition_manifest` to understand the motherboard's exact physical geometry (CPUs per socket, RAM banks).
- **Hard Thread Pinning**: Binds execution threads using `sched_setaffinity` and binds memory allocations using `mbind(MPOL_BIND)`. This guarantees that if the `vector_lane_engine` runs on Socket 0, its memory is exclusively allocated on Socket 0's local DRAM.
- **Hyper-Threading Avoidance**: Intelligently identifies and isolates sibling SMT (Simultaneous Multi-Threading) cores, ensuring hot-path threads are never starved of ALU execution ports by a background thread running on the same physical silicon.

## 3. Bibliography & Proofs
1. **Boyd-Wickizer, V., et al.** (2010). *An Analysis of Linux Scalability to Many Cores*. OSDI. (NUMA interconnect latency).
2. **Drepper, U.** (2007). *What Every Programmer Should Know About Memory*. (NUMA architectures and Thread Affinity).
3. **Broquedis, M., et al.** (2010). *hwloc: A Generic Framework for Managing Hardware Affinities in HPC Applications*. PDP.