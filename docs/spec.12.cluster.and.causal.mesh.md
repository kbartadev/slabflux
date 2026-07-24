# Cluster Connectivity & Causal Mesh

For globally distributed industrial and trading systems, the SLABFLUX implements a decentralized, wait-free coordination layer.

> **⚠️ ARCHITECTURAL PREVIEW**
> The Causal Mesh and Hybrid Logical Clock synchronization are currently in the research and design phase.

## `slabflux::cluster::causal_mesh`
A distributed, high-performance graph topology that preserves the exact relational state between events across disparate physical nodes.
* **Event Ordering via HLC:** Rather than relying on drift-prone physical timestamps, the mesh employs Hybrid Logical Clocks (`hlc_clock.hpp`) to enforce a strict, mathematically verifiable "happens-before" relationship. This guarantees that effects perpetually follow their underlying causes across the distributed network.
* **Wait-Free Consensus:** Empowers the cluster to achieve state agreement (e.g., determining the `failover_orchestrator` status) instantaneously. This wait-free architecture outright eliminates the severe latency penalties inherent in traditional leader-election protocols like Paxos or Raft.

## `slabflux::core::mesh_map`
A globally synchronized, wait-free shared-memory registry. It maintains the physical and logical blueprint of the entire active cluster, providing O(1) discovery for peer routing.
