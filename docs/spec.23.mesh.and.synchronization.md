# Mesh Topology & Low-Level Synchronization

Managing cohesive state across a physically distributed or massively parallel bare-metal cluster requires logical structures that map perfectly to the underlying hardware topology.

## `slabflux::core::mesh_map`
A wait-free, memory-mapped global registry maintaining the absolute physical and logical layout of the active SLABFLUX cluster.
* **Topology Awareness:** Pinpoints exactly which binary processes are executing on which specific physical CPU cores and NUMA nodes.
* **Inter-Process Communication (IPC):** Leverages `shm_open` and `mmap` to forge a unified, cross-process memory space.

## `slabflux::core::static_topology`
A constexpr-driven representation of the hardware environment.
* **Compile-Time Baking:** In deployments where the target silicon is known prior to compilation, `static_topology` physically bakes the NUMA node distances, cache hierarchies, and interconnect speeds directly into the compiled executable, eradicating all runtime discovery overhead.

## `slabflux::core::lsn_heartbeat`
A specialized watchdog tracking Logical Sequence Numbers (LSN) globally across all active `conduit` instances.
* **Microsecond Stall Detection:** Continuously audits producer LSN advancement. If an LSN fails to increment within a strictly configured microsecond threshold, it immediately triggers the `heartbeat_monitor` to quarantine the thread and investigate potential silicon-level hangs.
