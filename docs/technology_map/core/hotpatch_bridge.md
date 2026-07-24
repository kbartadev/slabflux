# SlabFlux Core: Hotpatch Bridge (`hotpatch_bridge.hpp`)

## 1. Architectural Overview
In high-availability HFT environments, halting the execution pipeline to update trading logic or AI weights is impossible. The `hotpatch_bridge` facilitates zero-downtime, instruction-level logic pivoting. It allows the management plane to seamlessly inject newly compiled domain handlers into the deterministic hot path on the fly.

## 2. Acquire/Release Pointer Pivoting
The bridge avoids using `std::shared_mutex` (which incurs expensive cache-line write locks for readers).
Instead, it relies on strict `std::memory_order` atomic semantics:
- The hot-path pipeline continuously accesses the active logic block via an `std::atomic<Handler*>` using `memory_order_acquire`.
- On modern x86-64, an acquire-load is essentially free, compiling down to a standard `MOV` instruction without explicit memory fencing.

## 3. The Hotpatch Lifecycle
When an update is deployed:
1. The management thread instantiates the new Handler in an isolated memory pool and warms its instruction cache via `pipeline_warmer`.
2. The thread executes an atomic Compare-And-Swap (CAS) with `memory_order_release` to swap the active pointer.
3. The next time the hot-path evaluates an event, it transparently loads the new pointer and executes the updated logic without dropping a single packet.

## 4. Safe Epoch Reclamation
The critical challenge is safely deleting the *old* logic block, as the hot path might currently be executing inside it.
The bridge utilizes an Epoch-based deferral mechanism (`hazard_pointers` or RCU-like delays):
- The old pointer is pushed to a `reclamation_queue`.
- The system waits for the hot path to advance its internal `LSN` (guaranteeing it has exited the previous function block).
- Only then is the memory associated with the old handler safely released back to the `eternal_memory` allocator.