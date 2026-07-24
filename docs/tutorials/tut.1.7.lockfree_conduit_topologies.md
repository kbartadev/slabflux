# Tutorial 1.7: Lock-Free Conduit Topologies

Standard lock-free queues often suffer from **Cache-Line Bouncing** (MESI thrashing) because both producer and consumer modify a shared head/tail index. SLABFLUX conduits use spatial isolation to ensure no two CPU cores contest the same metadata.

## 1. The Pendulum Architecture (`pendulum_spsc_conduit.hpp`)

The `pendulum_spsc_conduit` implements an **Oscillating Wavefront** traversal. Instead of wrapping around from $N \to 0$ (which disrupts hardware prefetchers), the cursors march forward across the array and then reverse direction.

*   **Zero-CAS**: Synchronization is achieved by using the payload slots themselves as the signal. A `nullptr` indicates a **VACUUM** (empty) state.
*   **Sovereign Cursors**: Producer and Consumer maintain strictly isolated local `cursor_state` structs, padded to avoid false sharing.
*   **Boustrophedon Traversal**: By oscillating the index instead of wrapping to zero, the hardware prefetcher maintains a 100% hit rate, as the access pattern is always a linear stride.

## 2. Hands-On: Wait-Free SPSC Transfer

```cpp
#include "slabflux/core/pendulum_spsc_conduit.hpp"
#include "slabflux/core/spsc_pool.hpp"

struct Task { int id; };

void producer_loop(slabflux::core::pendulum_spsc_conduit<Task, 1024>& conduit, 
                   slabflux::core::spsc_pool<Task, 1024>& pool) {
    Task* t = pool.make_raw(1);
    
    // try_push is wait-free. It fails only if the consumer is lagging.
    if (!conduit.try_push(t)) {
        pool.release(t);
    }
}

void consumer_loop(slabflux::core::pendulum_spsc_conduit<Task, 1024>& conduit) {
    Task* t = nullptr;
    
    // try_pop retrieves the pointer with zero atomic contention.
    if (conduit.try_pop(t)) {
        // Process Task...
    }
}
```

## 4. Why This Outperforms CAS

The Pendulum architecture eliminates the "Wrap-Around" penalty found in circular buffers. In a standard ring buffer, the jump from index $N-1$ to $0$ causes a pipeline stall and a prefetcher reset. 

By oscillating back and forth, the `pendulum_spsc_conduit` ensures that the consumer core is always following the producer core's linear memory heat, maximizing L1-D residency. Since only `nullptr` and `T*` are exchanged, there is no metadata contention on shared head/tail counters.