# Tutorial 6.3: Intel CAT Cache Isolation

## 1. The "Noisy Neighbor" Problem
Even if you have successfully pinned your Sovereign Core to a dedicated CPU thread using `topology_enforcer`, modern x86 architectures share the L3 Cache (Last Level Cache) across all physical cores on the same socket.

If a noisy neighbor process (e.g., OS telemetry, log compression, or SSH sessions) running on Core 1 performs massive memory sweeps, it will evict the deterministic Context Vaults and `spsc_pool` memory belonging to your Sovereign Core on Core 2. This causes sudden L3 Cache misses, resulting in unpredictable memory stalls.

## 2. Intel Cache Allocation Technology (CAT)
The `sys/cache_partitioner.hpp` module leverages Intel Resource Director Technology (RDT) to physically partition the silicon of the L3 cache.

By defining Classes of Service (CLOS), the `cache_partitioner` can allocate specific cache "ways" exclusively to the Sovereign Core's process ID. Once allocated, no other thread on the CPU socket can evict data from those cache lines.

## 3. Hands-On: Partitioning the L3 Cache

Because modifying cache allocations requires interacting with Model-Specific Registers (MSRs) or the Linux `resctrl` filesystem, the `cache_partitioner` is invoked during the Ignition Phase with elevated (root/CAP_SYS_ADMIN) privileges.

```cpp
#include "slabflux/sys/cache_partitioner.hpp"
#include "slabflux/sys/topology_enforcer.hpp"
#include <iostream>

int main() {
    std::cout << "[SYSTEM] Initiating L3 Cache Shielding...\n";

    // 1. Initialize the Intel CAT controller via resctrl boundary
    slabflux::sys::cache_partitioner partitioner;

    // 2. Define a strict Class of Service (CLOS) for the Sovereign Core
    // Bitmask 0x0F allocates 4 cache ways (assuming 20 total ways, e.g., 0xFFFFF)
    // This physically isolates ~20% of the L3 cache.
    uint32_t clos_id = partitioner.create_exclusive_clos(0x0F);

    // 3. Pin the current process to the new Cache Partition
    partitioner.assign_thread_to_clos(0, clos_id); // 0 = current thread

    // 4. Pin thread to Core 2
    slabflux::sys::topology_enforcer::pin_thread(2, 0);

    std::cout << "[SYSTEM] Thread secured in CAT Partition 0x0F. Entering Hot Path.\n";

    // ... Start Sovereign Core ...
    
    return 0;
}
```

## 4. Best Practices
*   **Capacity Planning:** Be extremely conservative with CAT masks. If you allocate too many cache ways to the Sovereign Core, you starve the Linux kernel and background drivers, potentially causing kernel panics or extreme system-wide I/O degradation.
*   **HugePages Integration:** `cache_partitioner` should always be paired with `pinned_allocator_spsc` (HugePages). Shielding standard 4KB virtual pages is largely ineffective because TLB misses will bottleneck the system before the L3 cache does.