# SlabFlux Mesh: Causal Ingress Router (`causal_ingress_router.hpp`)

## 1. Architectural Overview
The `causal_ingress_router` is an O(1) wait-free frame router that guarantees strict causal ordering before network events are permitted to mutate the deterministic compute engine.

## 2. Out-of-Order Parking Lot
In distributed meshes, packets frequently arrive out of sequence.
- The router maintains a fixed-size, zero-allocation ring buffer (the Parking Lot).
- If a packet arrives prematurely (e.g., LSN 5 arrives before LSN 4), it is instantly slotted into the parking lot via O(1) bitwise masking `seq & MASK`.
- No dynamic allocation or priority-queue sorting occurs on the hot path.

## 3. Cascade Unblocking
When the missing sequence (LSN 4) arrives, the router forwards it to the Engine. It then immediately checks the parking lot for LSN 5. Finding it, it forwards LSN 5, draining the parked frames in a continuous, rapid cascade until the causal sequence is fully restored.

## 4. Instant Memory Recycling
The router accepts a type-erased memory pool pointer (`free_fn_`). When a packet is fully processed, or if it is dropped due to an unrecoverable sequence gap, the router instantly recycles the physical frame memory back to the pinned SPSC pool, preventing memory leaks during massive NACK storms.