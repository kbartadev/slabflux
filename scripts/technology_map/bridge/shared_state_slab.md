# SlabFlux Bridge: Shared State Slab (`shared_state_slab.hpp`)

## 1. Architectural Overview
In multi-process topologies, read-only analytics clients (like risk engines or web dashboards) need access to the live execution state without locking or slowing down the primary trading engine. The `shared_state_slab` provides a wait-free, zero-copy memory mirror across process boundaries.

## 2. Double-Buffered Epoch Memory
The slab utilizes an Epoch-based Double Buffering architecture inside `/dev/shm`:
- **Active Buffer**: The memory block actively being read by external observer processes.
- **Back Buffer**: The memory block actively being mutated by the deterministic `branchless_engine`.

## 3. O(1) State Publishing
When the deterministic engine finishes a processing cycle and wishes to publish its state:
1. It executes an atomic `memory_order_release` toggle, swapping the Active and Back buffer pointers.
2. External readers using `std::atomic::load(memory_order_acquire)` instantly see the new pointer and seamlessly transition to reading the newly finalized state block.
3. The engine immediately begins overwriting the old Active buffer (which is now the Back buffer) on the next tick.

## 4. Dirty-Read Prevention
To guarantee that readers never read a partially updated matrix, the `shared_state_slab` integrates tightly with the `sovereign_observer` SeqLock logic. Observers check the epoch marker before and after reading the memory slab. If the engine swapped the buffers mid-read, the observer detects the sequence change and retries the copy, ensuring absolute visual consistency across the IPC boundary.