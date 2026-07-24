# Tutorial 04: Lock-Free Conduits & Phase Sync

## 1. The Cost of Compare-And-Swap (CAS)

Standard multi-threading systems rely on OS-level mutexes or atomic `Compare-And-Swap` (CAS) loops. In highly concurrent architectures (like HFT or distributed data flow), CAS loops trigger Read-For-Ownership (RFO) cache invalidation storms on the interconnect bus (e.g., Intel QPI or AMD Infinity Fabric). 

When multiple cores contest a single atomic index, the interconnect bus saturates, spiking tail latency from nanoseconds to microseconds. SlabFlux explicitly prohibits standard atomic CAS operations across deterministic boundaries.

## 2. Monotonic Phase Matching

To communicate across decoupled topologies (e.g., Ingress Core to Sovereign AI Core, or CPU to GPU), SlabFlux uses **Monotonic Phase Matching** inside **Conduits**.

A Conduit is a lock-free, wait-free ring buffer. Instead of contesting a global atomic read/write index, synchronization is achieved by embedding an 8-bit `phase_tag` at the end of every payload struct.

*   **Writer:** Constructs the payload locally, issues a Release barrier (`_mm_sfence` or `__atomic_store_n`), and updates the phase tag directly in the conduit memory.
*   **Reader:** Continuously polls the phase tag with an Acquire barrier. If the tag matches the next expected internal phase, the payload is verified as complete and safe to read.

## 3. Hands-On: SPSC Conduit Polling

Here is a look at the underlying architecture of a Single-Producer Single-Consumer (SPSC) conduit reading loop:

```cpp
#include <cstdint>

// Ensure the payload perfectly tiles into a 64-byte cache line
struct alignas(64) conduit_slot {
    uint64_t payload_ptr;
    uint64_t metadata;
    uint32_t padding[11];
    uint32_t phase_tag; // Embedded exactly at the end of the cache line
};

// Reader logic running on the deterministic hot-path
void poll_conduit(conduit_slot* ring, uint32_t ring_size, 
                  uint32_t& current_idx, uint32_t& expected_phase) {
    
    // Zero-abstraction TSO acquire load - compiles to a single 'mov' instruction on x86
    uint32_t tag = __atomic_load_n(&ring[current_idx].phase_tag, __ATOMIC_ACQUIRE);

    if (tag == expected_phase) {
        // Process payload safely
        dispatch_payload(ring[current_idx].payload_ptr);

        // O(1) modulo logic using bitmasks (ring_size must be a power of 2)
        current_idx = (current_idx + 1) & (ring_size - 1);

        // Wrap-around phase tagging guarantees ABA safety
        if (current_idx == 0) [[unlikely]] {
            expected_phase = (expected_phase + 1) & 0xFF;
            if (expected_phase == 0) expected_phase = 1; // 0 reserved for VACUUM state
        }
    }
}
```

## 4. Why This Outperforms CAS

By coupling the data payload and the synchronization phase tag into the *exact same 64-byte cache line*, the CPU memory controller fetches both in a single hardware transaction.

Furthermore, because the phase linearly increments and wraps around synchronously, the **ABA problem** is mathematically eliminated without requiring dual-word atomics (`CMPXCHG16B` or 128-bit CAS), resulting in completely lock-free, zero-contention cross-core data streaming.