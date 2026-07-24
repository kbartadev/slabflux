# SlabFlux Dist: Causal Sequencer (`causal_sequencer.hpp`)

## 1. Architectural Overview
In a multi-node, peer-to-peer execution matrix, events can arrive from multiple sources simultaneously. The `causal_sequencer` guarantees that the distributed state machine honors Lamport's "happens-before" relationship, preventing temporal paradoxes across the mesh.

## 2. Vector Clock Horizons
Instead of a single scalar Sequence Number, the sequencer maintains a Vector Clock tracking the logical progression of every node in the cluster.
- When Node A broadcasts an event, it attaches its current Vector Horizon.
- Node B evaluates the incoming vector against its local vector.
- If the incoming vector indicates that Node A processed an event from Node C that Node B hasn't seen yet, Node B deterministically quarantines Node A's event in the `hole_puncher` until Node C's missing payload arrives.

## 3. Deterministic Convergence
By strictly enforcing causal ordering before injecting events into the `pipeline`, the `causal_sequencer` guarantees that regardless of network jitter, packet reordering, or switch congestion, every node in the SlabFlux cluster evaluates the computational state graph in the exact same sequence. This ensures absolute bit-parity across distributed memory matrices.