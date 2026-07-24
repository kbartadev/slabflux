# Advanced Bridge Primitives & State Buffers

Beyond foundational conduits, the `slabflux::bridge` namespace provisions specialized synchronization primitives engineered for complex, multi-threaded state sharing without ever yielding to the OS via traditional mutexes.

## Shared State & Cross-Thread Signaling
* **`shared_state_buffer.hpp`**: A wait-free, sequentially consistent memory buffer. It is specifically designed for broadcasting rapidly mutating global configurations or massive market-state snapshots across multiple isolated threads without utilizing SeqLocks or atomics that would dirty cache lines.
* **`signal_backbone.hpp` & `engine_pulse.hpp`**: Constitutes a sub-microsecond, cross-thread signaling backplane. The `engine_pulse` serves as a deterministic global metronome, enabling multiple independent pipelines to synchronize their execution phases (e.g., aligning network batch-reads) flawlessly across the NUMA topology.
* **`bridge_sync.hpp`**: Houses the proprietary memory visibility fences and SlabFlux-specific synchronization invariants required for cross-core state consistency.
* **`input_conduit.hpp`**: A highly specialized SPSC conduit utilizing C++20 `<bit>` operations and `std::hardware_constructive_interference_size` for absolute cache sovereignty. It is engineered to interface directly with memory-mapped ring buffers populated natively by the NIC via kernel bypass, using a proprietary memory model that avoids canonical public patterns.
