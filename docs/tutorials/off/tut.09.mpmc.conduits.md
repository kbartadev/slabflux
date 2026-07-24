# Tutorial 09: MPMC Conduits & Mesh Contention

## 1. Expanding Beyond Single-Producer (SPSC)

In previous modules (Tutorial 04 & 06), we utilized SPSC (Single-Producer Single-Consumer) conduits to ingest data. While SPSC provides the absolute lowest latency bounds due to zero cache-line contention, production deployments often use modern NICs (like Mellanox ConnectX-6) configured with Receive-Side Scaling (RSS). 

RSS distributes incoming network packets across multiple hardware queues. We must deploy multiple Ingress threads to poll these queues. If they all feed a single Sovereign Core, we require a **Multi-Producer Single-Consumer (MPMC)** topology.

## 2. The Danger of Locking

If multiple producers attempt to acquire an OS-level mutex (`std::mutex`) or a spinlock before writing to the conduit, the system will collapse under high message rates (e.g., 10M+ msgs/sec). Threads will deschedule, and the interconnect bus will be flooded with Read-For-Ownership (RFO) invalidation packets.

## 3. Wait-Free MPMC via Independent Phase Calculation

SlabFlux solves this using an atomic Fetch-And-Add (FAA) solely on the reservation index, combined with our Monotonic Phase Matching.

Because the conduit size is a power of 2, a producer can atomically claim an absolute sequence number and mathematically derive **both** its slot index and its correct phase tag entirely independently of other threads.

### Hands-On: Lock-Free MPMC Push

```cpp
#include <cstdint>

struct alignas(64) mpmc_slot {
    void* payload;
    uint32_t length;
    uint32_t padding[10];
    uint32_t phase_tag;
};

struct mpmc_conduit {
    mpmc_slot* ring;
    uint32_t ring_size; // MUST be a power of 2
    
    // Padded to prevent False Sharing with the payload memory
    alignas(64) uint32_t claim_index{0}; 

    // Executed concurrently by MULTIPLE Ingress threads
    void push(void* data, uint32_t len) {
        // 1. Atomically reserve an absolute ticket (Hardware optimized XADD)
        uint32_t ticket = __atomic_fetch_add(&claim_index, 1, __ATOMIC_RELAXED);
        
        // 2. O(1) mathematical routing
        uint32_t actual_idx = ticket & (ring_size - 1);
        
        // 3. Derive the specific phase tag for this exact lap
        // 0 is reserved for VACUUM, so we use 1-255 modulo math
        uint32_t lap_count = ticket / ring_size;
        uint32_t calculated_phase = (lap_count % 255) + 1;

        // 4. Write the payload
        ring[actual_idx].payload = data;
        ring[actual_idx].length = len;

        // 5. Store Fence to ensure data settles before phase publication
        asm volatile("sfence" ::: "memory");

        // 6. Release the phase (Reader is unblocked)
        __atomic_store_n(&ring[actual_idx].phase_tag, calculated_phase, __ATOMIC_RELEASE);
    }
};
```

## 4. The Sequential Reader Guarantee

Even though Producer B might claim ticket `1` and finish writing *before* Producer A finishes writing ticket `0`, the Sovereign Core (Reader) sequentially polls index `0` for phase `1`, then index `1` for phase `1`. 

The reader will automatically spin on index `0` until Producer A completes its write, guaranteeing strict sequential determinism inside the execution manifold without a single mutex.

## 5. Best Practices & Anti-Patterns
*   **Best Practice: Ticket batching.** If an Ingress thread receives a burst of 32 packets from DPDK, do not call `__atomic_fetch_add` 32 times. Call it once adding `32` to reserve a contiguous block of tickets, reducing bus contention by 97%.
*   **Anti-Pattern: Mid-write Yielding.** A producer thread must **never** block, sleep, or yield to the OS after claiming a ticket but before publishing the phase tag. Doing so will freeze the Sovereign Core reader (which is spinning on that ticket's phase) and halt the entire pipeline.