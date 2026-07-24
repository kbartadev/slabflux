# SlabFlux Core: Causal Mesh (`causal_mesh.hpp`)

## 1. Architectural Overview
The `causal_mesh` is the overarching topological map that defines the interconnected sovereignty of SlabFlux nodes. Rather than treating instances as isolated applications communicating blindly via TCP/IP, the mesh treats all configured cluster nodes as disjoint memory regions participating in a unified, deterministic state machine.

## 2. Deterministic Routing Topology
To maintain O(1) multi-node routing, dynamic discovery protocols (like mDNS) are restricted to the initialization phase.
Once the `ignition_manifest` seals the boot sequence, the `causal_mesh` compiles a static layout:
- Every node in the cluster is assigned an immutable 8-bit `node_id`.
- The routing tables are flattened into an O(1) array (the `node_directory`).
- When a trade or AI stimulus needs to be forwarded to a specific execution shard, the mesh looks up the target socket/MAC address instantaneously via array indexing, avoiding associative maps.

## 3. Happens-Before Sequence Enforcement
The mesh enforces global transaction order using the `causal_sequencer`.
- All packets traversing the mesh are stamped with the Authoritative Node's LSN and the `hlc_clock` timestamp.
- If a node receives an event out of order (e.g., a switch drops a packet, causing LSN 105 to arrive before LSN 104), the `causal_mesh` physically quarantines LSN 105 in a `hole_puncher` buffer.
- The execution core stalls its progression on that specific stream until the `nack_handler` recovers LSN 104, ensuring the deterministic state matrix is never evaluated out-of-sequence.

## 4. Origin Reset and Spatial Quarantine
If a remote node suffers a catastrophic panic and restarts, its local state is wiped. 
The `causal_mesh` coordinates the `origin_reset_event`:
- When the rebooted node reconnects, the mesh broadcasts a reset signal across the cluster.
- The local `branchless_engine` traps this signal and uses non-temporal stores (`_mm512_stream_ps`) to zero out *only* the specific memory shard owned by the restarting node, leaving the rest of the cluster's active state completely untouched.