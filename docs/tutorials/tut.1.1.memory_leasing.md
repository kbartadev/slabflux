# Tutorial 1.1: O(1) Memory Leasing

## 1. The Zero-Allocation Hot Path
Standard C++ memory allocation (`malloc`, `new`, `std::make_shared`) invokes the OS kernel memory manager. This introduces non-deterministic locks, page faults, and context-switch latency spikes. In the SlabFlux RTE, dynamic allocation is strictly confined to the ignition phase. On the hot path, memory is leased in $O(1)$ time.

## 2. Physical Memory Hardening
The `spsc_pool` natively manages its own hardened memory mapping, bypassing standard allocators entirely.

*   **HugePage Residency**: The pool attempts to allocate 2MB HugePages (`MAP_HUGETLB | MAP_HUGE_2MB`) to eliminate TLB misses.
*   **Physical Pinning (`mlock`)**: It permanently locks memory pages into RAM (`MAP_LOCKED`). The OS virtual memory manager is forbidden from swapping these pages to disk, completely eradicating page-fault latency on the hot path.
*   **Graceful Fallbacks**: If the OS denies HugePages or locked memory (e.g., due to `ulimit -l`), the pool gracefully downgrades to standard anonymous pages, ensuring the application still boots.

## 3. The Lock-Free Pool (`spsc_pool.hpp`)
The `spsc_pool` is a highly optimized, lock-free, LIFO (Last-In-First-Out) memory manager.

### Cache Warming and LIFO
Because the pool operates as a LIFO stack via the internal `free_ring_`, the most recently relinquished memory address is the first to be reallocated. This guarantees that the recycled object is highly likely to remain resident in the CPU's L1 Data Cache.

### Dangling Pointer Mitigation
The pool integrates directly with the SPSC wire. Calling `release(ptr)` pushes the pointer back into the internal `free_ring_`. Furthermore, the pool supports SIMD-accelerated invalidation (`invalidate(ptr)`), which leverages the conduit's AVX-512 backend to purge polluted pointers during journal re-entry.

## 4. RAII Lifecycle (`managed_data.hpp`)
Raw pointers (`T*`) are dangerous in complex routing topologies. `managed_data<T>` provides a zero-overhead RAII wrapper to guarantee leak-free lifecycles.

*   **Zero Allocator Overhead**: `managed_data` wraps a `scoped_ptr` containing the raw pointer, the origin pool pointer, and a static `deleter_fn`. No virtual destructors or control blocks are used.
*   **Conduit Integration**: SlabFlux conduits (like `spsc_conduit`) natively accept `managed_data` in their `try_push` methods. When successfully pushed to a wire, the conduit automatically calls `release()` on the handle, stripping ownership without destroying the underlying memory.

### Hands-On: Leasing and Releasing

```cpp
#include "slabflux/core/spsc_pool.hpp"
#include "slabflux/core/managed_data.hpp"

struct MarketTick {
    MarketTick(uint64_t id, double p) : instrument_id(id), price(p) {}
    uint64_t instrument_id;
    double price;
};

int main() {
    slabflux::core::spsc_pool<MarketTick, 1024> pool;
    {
        // make() returns managed_data<MarketTick, spsc_pool> via perfect forwarding
        auto tick = pool.make(12345, 150.25); 

        if (tick->price < 0.0) {
            return -1; // RAII returns pointer to pool automatically
        }
    } // tick goes out of scope; pool.release() is called.
    
    return 0;
}
```