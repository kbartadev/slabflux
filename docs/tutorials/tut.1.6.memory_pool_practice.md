# Tutorial 1.6: The Pool Lifecycle & Perfect Forwarding

Dynamic heap operations (`new`, `malloc`, or `std::make_shared`) are fundamentally non-deterministic. They trigger kernel-level sync mechanisms and memory fragmentation. SLABFLUX eliminates this by utilizing the `spsc_pool<T, Capacity>`, a strict Single-Producer Single-Consumer lock-free memory manager.

## 1. Allocation via Perfect Forwarding

The `spsc_pool` manages a contiguous memory block, physically hardened using HugePages and `mlock` (see `tut.1.1`). To generate an event, you can use `make_raw(...)` for raw pointers or `make(...)` for RAII-managed data.

```cpp
#include "slabflux/core/spsc_pool.hpp"
#include <iostream>

struct NetworkPacket {
    int session_id;
    NetworkPacket(int sid) : session_id(sid) {}
};

int main() {
    // 1. Instantiate the pool (Statically sized at 1024 elements)
    slabflux::core::spsc_pool<NetworkPacket, 1024> pool;

    // 2. Raw Allocation (Wait-free O(1))
    // Returns NetworkPacket* or nullptr if the free_ring is empty.
    NetworkPacket* raw_packet = pool.make_raw(42);
    if (raw_packet) {
        std::cout << "Raw Session: " << raw_packet->session_id << "\n";
        pool.release(raw_packet); // Explicit return to free-list
    }

    // 3. Managed Allocation (RAII)
    // Returns managed_data<NetworkPacket, spsc_pool>
    auto managed = pool.make(84);
    
    return managed ? 0 : 1;
}
```

## 2. Explicit Lifecycle Management

When using `make_raw()`, you must explicitly release the memory back to the pool to prevent resource starvation.

```cpp
void processing_loop(slabflux::core::spsc_pool<NetworkPacket, 1024>& pool) {
    // make_raw returns a pointer (wait-free)
    auto* ev = pool.make_raw(0); 
    
    if (!ev) return;
    if (ev->session_id == 0) {
        pool.release(ev);
        return;
    }
    
    // Normal processing...
    
    // Terminal endpoint must release the memory
    pool.release(ev);
}
