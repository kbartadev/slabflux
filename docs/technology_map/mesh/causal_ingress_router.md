# Foundation: Causal Ingress Router (`slabflux/mesh/causal_ingress_router.hpp`)

## 1. Architectural Justification
In distributed meshes without centralized locking, strict chronological parity is mathematically impossible to guarantee over UDP transmission. The `causal_ingress_router` enforces Lamport's Happens-Before invariant locally by isolating "future" packets in a constant-time parking lot until antecedent packets arrive, preventing split-brain execution states.

## 2. Hardware Implementation Directives
- **O(1) Out-Of-Order Parking**: Implements a zero-allocation, bounded pointer array (`wire_frame* parked_frames_[]`). OOO packets are resolved to array indices using bitwise masking, avoiding O(log N) heap-allocated priority queues.
- **Instant Cascade Unlocking**: When sequence gaps are closed, the router iterates sequentially to unblock all contiguous parked frames in a single unrolled loop, restoring chronological continuum deterministically.

## 3. Bibliography & Proofs
1. **Lamport, L.** (1978). *Time, Clocks, and the Ordering of Events in a Distributed System*. Communications of the ACM, 21(7), 558-565. (The foundational theorem of sequence causality and Happens-Before relations).
2. **Mattern, F.** (1989). *Virtual Time and Global States of Distributed Systems*. Parallel and Distributed Algorithms.
3. **Birman, K. P., & Joseph, T. A.** (1987). *Reliable communication in the presence of failures*. ACM Transactions on Computer Systems (TOCS).