# Foundation: Causal Determinism & State Machine Replication

## 1. Architectural Justification
High-availability (HA) clusters in trading environments often suffer from Split-Brain syndromes and non-deterministic divergence. If an Active and a Passive node rely on physical system clocks (`std::chrono`) or local thread-scheduling interleavings to resolve event priorities, their topological states will inevitably diverge over millions of operations.

SlabFlux enforces Absolute Distributed Determinism via the State Machine Replication (SMR) pattern. By discarding physical time in favor of Logical Sequence Numbers (LSNs) and Epoch Aliasing, all events exist within a rigid causal mesh. A node merely applies state transitions $S_{n} = f(S_{n-1}, E_n)$. If the sequence $E_n$ is identical, the physical memory state $S_n$ across all nodes is mathematically guaranteed to be identical.

## 2. Hardware Implementation Directives
- **Epoch Arbiters and Hole Punching:** Sockets buffering UDP multicast traffic pass through `hole_puncher` boundaries. Out-of-order LSNs are quarantined in $O(1)$ time, and missing sequences are requested via NAKs before execution.
- **Snapshot Engines:** Replaying the entire DAG from ignition is too slow for instantaneous failover. The `snapshot_engine` captures continuous cache-aligned memory states asynchronously, allowing a passive node to adopt the Active Sovereign role in microseconds.
- **Sovereign Ignition Sealing:** Once the startup phase completes, dynamic resource acquisition (memory, file descriptors) is locked. The pipeline becomes a pure, sealed Cartesian dispatch manifold.

## 3. Bibliography & Proofs
1. **Lamport, L.** (1978). *Time, clocks, and the ordering of events in a distributed system*. Communications of the ACM. (The fundamental proof that logical clocks and causal sequencing are required over physical clocks to maintain distributed system consistency).
2. **Schneider, F. B.** (1990). *Implementing fault-tolerant services using the state machine approach: A tutorial*. ACM Computing Surveys. (The basis of state-machine replication, snapshot recovery, and input determinism).
3. **Castro, M., & Liskov, B.** (1999). *Practical Byzantine Fault Tolerance*. OSDI. (Proofs on ensuring system resilience in distributed meshes given arbitrary hardware node failures or network drops).
4. **Oki, B. M., & Liskov, B. H.** (1988). *Viewstamped Replication: A new primary copy method to support highly-available distributed systems*. PODC. (Early foundations on epoch-driven view changes and active-passive failover orchestrations).