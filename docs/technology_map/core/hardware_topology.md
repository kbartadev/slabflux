# SlabFlux Core: Hardware Topology (`hardware_topology.hpp`)

## 1. Architectural Overview
Software layout in SlabFlux operates subservient to hardware mechanics. The `hardware_topology` system ensures that Thread and Memory affinity parameters are strictly governed to maintain physical locality across multi-socket systems.

## 2. NUMA Locality
Non-Uniform Memory Access (NUMA) topologies mean that accessing memory attached to a different physical CPU socket incurs significant latency across the QPI/Infinity Fabric.
- `hardware_topology` utilizes `mbind` to enforce memory allocation policies, ensuring that core memory pools (`pinned_allocator`) are strictly allocated on the same physical NUMA node as the executing thread.
- Cross-socket memory blending is actively blocked, resolving memory bottlenecks at the structural level.

## 3. Thread Pinning
- Exposes strict `pin_thread_to_core` primitives utilizing the Linux `sched_setaffinity` API and `pthread_setaffinity_np`.
- Locks specific pipeline stages (e.g., `ingress`, `compute`, `journal`) to explicit logical cores.
- Works in tandem with kernel boot parameters (`isolcpus`, `nohz_full`) to prevent the OS scheduler from migrating the hot path or interrupting it with background tasks.

## 4. L2/L3 Cache Isolation Checks
Provides topology audits via `verify_l2_isolation()`:
- Scans `sysfs` topology maps to ensure that highly contentious pipelines (like Ingress SQPOLL threads and Deterministic Compute threads) do not share the same physical L2 cache slice.
- Prevents priority inversion and destructive L2 cache eviction storms during heavy bursts.