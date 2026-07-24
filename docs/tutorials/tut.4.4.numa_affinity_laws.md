# Tutorial 9: Hardware Topology & Cache Line Isolation

In ultra-low latency development, how your data sits in physical RAM is just as important as your algorithmic complexity. Modern CPUs fetch memory in 64-byte chunks called "Cache Lines". 

If two independent threads modify variables that happen to live on the exact same 64-byte cache line, the CPU cores will constantly invalidate each other's L1/L2 caches. This phenomenon is called **False Sharing**, and it can degrade performance by orders of magnitude. SLABFLUX mandates strict cache-line isolation.

## 1. Enforcing Hardware Alignment

To prevent False Sharing and guarantee that your events fit perfectly into CPU cache lines, SLABFLUX utilizes C++20 `std::hardware_constructive_interference_size` to determine alignment boundaries dynamically based on target silicon, rather than using fixed-offset padding common in public implementations.

```cpp
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core.hpp"

// BAD: Compiler might pack this adjacent to other data, causing False Sharing
struct bad_event {
    bool is_active;
    double value;
};

// GOOD: Strictly isolated based on physical hardware properties.
struct alignas(std::hardware_constructive_interference_size) optimized_event {
    bool is_active;
    double value;
    
    optimized_event(bool active, double val) : is_active(active), value(val) {}
};
```

## 2. NUMA-Aware Memory Allocation

In multi-socket server motherboards (like Dual EPYC or Xeon setups), memory is divided into NUMA (Non-Uniform Memory Access) nodes. If CPU 1 tries to access memory physically attached to CPU 2, it suffers a massive latency penalty traversing the QPI/Infinity Fabric bridge.

SLABFLUX provides the `hardware_topology` interface to guarantee that your memory pools are allocated exactly on the local RAM bank of the executing CPU core.

```cpp
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/pool.hpp"

void initialize_numa_aware_system() {
    // 1. Pin the current thread to a specific CPU core (e.g., Core 4)
    slabflux::core::hardware_topology::pin_thread_to_core(4);
    
    // 2. Instruct the OS to allocate the pool strictly on the local NUMA node
    slabflux::core::hardware_topology::enforce_local_numa_allocation();
    
    // 3. Now instantiate the pool. The OS guarantees the physical memory 
    // is adjacent to Core 4.
    slabflux::core::spsc_pool<optimized_event, 4096> local_pool;
}
```
