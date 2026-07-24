# Tutorial 1.8: Orthogonal Manifolds & MPMC Contention

Multi-Producer Multi-Consumer (MPMC) queues are the primary bottleneck in high-frequency state machines. Standard implementations use a single shared atomic counter for `tail` and `head`, causing an interconnect bus storm (RFO) whenever multiple cores push simultaneously.

## 1. The Orthogonal Dimension-Routed Grid (`orthogonal_manifold.hpp`)

The `orthogonal_manifold` eliminates central contention by scattering threads across a 2D matrix of atomic pointers.

*   **Horizontal Producers**: Producers use a hardware-entropy derived `thread_seed()` to select a specific row. They scan horizontally along a single cache line. If the line is full, they perform an affine jump to the next row.
*   **Vertical Consumers**: Consumers scan vertically, intersecting with producers at precisely one cell per row.

## 2. Hands-On: Wait-Free MPMC

```cpp
#include "slabflux/core/orthogonal_manifold.hpp"
#include "slabflux/core/spsc_pool.hpp"

struct StateUpdate { uint64_t lsn; };

int main() {
    // 1. Instantiate a manifold with 128 cache-line rows
    slabflux::core::orthogonal_manifold<StateUpdate, 128> manifold;
    slabflux::core::spsc_pool<StateUpdate, 1024> pool;

    // 2. Wait-Free Push (Horizontal Scan)
    StateUpdate* update = pool.make_raw(1001);
    if (!manifold.push(update)) {
        // All 128 rows are saturated
        pool.release(update);
    }

    // 3. Lock-Free Pop (Vertical Scan)
    StateUpdate* extracted = manifold.pop();
    if (extracted) {
        // Process...
        pool.release(extracted);
    }
}
```
