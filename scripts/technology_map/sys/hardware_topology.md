# SlabFlux Sys: Hardware Topology (`hardware_topology.hpp`)

## 1. Architectural Overview
SlabFlux considers the CPU not as a generic execution unit, but as a physical silicon landscape. The `hardware_topology` module interrogates and enforces strict geographical boundaries over memory controllers, PCIe lanes, and CPU cores to eliminate interconnect cross-talk and L3 cache misses.

## 2. NUMA Sovereignty
In multi-socket servers, accessing memory managed by a remote CPU socket (Cross-NUMA access) introduces devastating latency penalties.

### Topology Scanner
At boot, the `topology_scanner` utilizes `hwloc` (or native sysfs parsing) to map the physical motherboard. It identifies:
- Which CPU cores share the same L3 cache slice.
- Which NUMA node directly controls the PCIe slot hosting the primary network interface card (NIC).

### Strict Affinity Enforcement
The `topology_enforcer` ensures that the `baremetal_egress`, `af_xdp_ingress`, and `branchless_engine` threads are pinned exclusively to cores on the NIC's local NUMA node. Furthermore, all memory allocations (`hugepage_allocator`, `mpmc_pool`) are strictly bound to the local memory controller via `mbind()` and `set_mempolicy()`.

## 3. Cache Allocation Technology (Intel CAT)
Modern OS background tasks (SSH, metrics daemons) frequently pollute the L3 cache, evicting critical trading structures.

The `cache_partitioner` communicates directly with Intel's RDT (Resource Director Technology) MSRs:
- It slices the physical L3 cache into explicit partitions (Classes of Service).
- It assigns the SlabFlux deterministic cores to an isolated partition (e.g., granting them exclusive rights to 80% of the L3 cache).
- It forces all OS interrupts and background tasks into a restricted, non-overlapping partition, achieving true silicon isolation.

## 4. Hardware SMT/Hyper-Threading Resolution
Hyper-Threading shares physical ALU resources between logical cores. The topology engine actively identifies sibling threads. It mathematically prevents the deployment of critical hot-path loops on sibling pairs, guaranteeing 100% unimpeded execution port availability.