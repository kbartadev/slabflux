# SlabFlux RTE: Hands-On Exercise Solutions

This document contains the reference solutions for the exercises outlined in the SlabFlux Master Curriculum. These exercises demonstrate how to compose foundational routing, memory, and orchestration modules into unified, zero-allocation execution topologies.

---

## Exercise 1: Lock-Free Pipeline Transfer
**Task:** Create an `spsc_pool`, generate a raw event pointer, route it through an `spsc_conduit`, and safely invoke `pool.release(ptr)` on the receiving end.

**Solution & Breakdown:**
This exercise demonstrates how to safely pass dynamically pooled memory across an isolation boundary (e.g., Ingress Thread $\rightarrow$ Sovereign Core) without invoking heap allocators or `std::shared_ptr` reference-counting contention.

```cpp
#include "slabflux/core/spsc_pool.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include <iostream>
#include <thread>

struct MarketEvent {
    uint64_t timestamp;
    double price;
};

int main() {
    // 1. Allocate the physical memory pool (1024 elements, HugePage backed automatically)
    slabflux::core::spsc_pool<MarketEvent, 1024> pool;
    
    // 2. Instantiate the wait-free conduit (passing pointers, not by-value structs)
    slabflux::core::spsc_conduit<MarketEvent*, 1024> conduit;

    std::thread sovereign_core([&]() {
        MarketEvent* received_event = nullptr;
        
        // Poll the conduit until an event arrives
        while (!conduit.try_pop(received_event)) {
            asm volatile("pause" ::: "memory");
        }
        
        std::cout << "[CORE] Processed price: " << received_event->price << "\n";
        
        // Safely return the memory to the pool in O(1) time
        pool.release(received_event);
    });

    // Ingress Thread Logic
    // Use the managed_data wrapper for exception safety
    auto managed_event = pool.make(1625091823, 4500.50);
    
    // Push the managed handle into the conduit.
    // The conduit natively recognizes managed_data, extracts the raw pointer,
    // pushes it to the ring, and safely releases the handle without destroying the memory!
    while (!conduit.try_push(managed_event)) {
        asm volatile("pause" ::: "memory");
    }

    sovereign_core.join();
    return 0;
}
```

---

## Exercise 2: Wait-Free Ingress Sharding
**Task:** Utilize `round_robin_switch` to distribute a stream of 1,000,000 integers evenly across four downstream `mpmc_conduit` queues without a single failed CAS loop.

**Solution & Breakdown:**
This validates the ability to fan-out massive ingress telemetry to multiple worker threads without relying on standard OS locking mechanisms. The `round_robin_switch` automatically bypasses full conduits to maintain wait-free progression.

```cpp
#include "slabflux/bridge/round_robin_switch.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include <iostream>

int main() {
    constexpr int NUM_WORKERS = 4;
    
    // Create an array of 4 MPMC conduits, each holding 1024 items
    using TargetConduit = slabflux::core::mpmc_conduit<int*, 1024>;
    TargetConduit worker_queues[NUM_WORKERS];
    
    // Instantiate the switch
    slabflux::bridge::round_robin_switch<int, NUM_WORKERS> sharder;
    for (std::size_t i = 0; i < NUM_WORKERS; ++i) {
        sharder.bind_track(i, worker_queues[i]);
    }
    
    for (int i = 0; i < 1'000'000; ++i) {
        int* payload = new int(i); // Note: Pool allocation elided for brevity
        
        // route() distributes the pointer to the next available worker.
        while (!sharder.route(payload)) {
            // Only reached if ALL 4 worker conduits are simultaneously 100% full
            asm volatile("pause" ::: "memory"); 
        }
    }
    
    std::cout << "[SYSTEM] 1,000,000 items sharded successfully without locks.\n";
    return 0;
}
```

---

## Exercise 3: Sub-Nanosecond Telemetry Validation
**Task:** Query the CPU Time Stamp Counter via `__rdtsc()`, execute a dummy pipeline loop, and measure the exact hardware cycle latency of the iteration.

**Solution & Breakdown:**
Because `std::chrono` relies on VDSO or system calls (which can take 10-25 nanoseconds just to execute), measuring a pipeline that processes in under 5 nanoseconds requires reading the CPU's TSC (Time Stamp Counter) directly.

```cpp
#include "slabflux/core/pipeline.hpp"
#include <iostream>
#include <x86intrin.h> // For __rdtsc()

struct DummyEvent { int data; };

struct PassthroughHandler {
    void on(DummyEvent&) { /* No-op to measure baseline pipeline overhead */ }
};

int main() {
    PassthroughHandler handler;
    slabflux::core::pipeline<PassthroughHandler> pipe(handler);
    DummyEvent e{0};

    // Warmup the instruction cache
    for(int i=0; i<1000; ++i) pipe.dispatch(e);

    // Read the timestamp counter before execution
    // _mm_lfence() prevents the CPU from speculatively executing the pipeline early
    _mm_lfence();
    uint64_t start_cycles = __rdtsc();
    _mm_lfence();
    
    pipe.dispatch(e);
    
    _mm_lfence();
    uint64_t end_cycles = __rdtsc();
    _mm_lfence();

    std::cout << "[METRICS] Pipeline Dispatch Cost: " 
              << (end_cycles - start_cycles) << " CPU Cycles.\n";
              
    return 0;
}
```

---

## Exercise 4: Out-of-Order Quarantine
**Task:** Instantiate a `hole_puncher`. Inject LSN 2, 4, and 5. Verify quarantine. Inject LSN 3 and trigger the `__builtin_ctzll` cascade flush.

**Solution & Breakdown:**
This simulates a Causal Mesh receiving UDP multicast packets out of order. Instead of blocking or sorting on a heap-allocated tree, the `hole_puncher` uses hardware bit-scanning to flush packets exactly when the gap is filled.

```cpp
#include "slabflux/core/hole_puncher.hpp"
#include <iostream>

struct NetworkPacket { uint64_t lsn; };

int main() {
    slabflux::core::hole_puncher<NetworkPacket, 64> quarantine;
    uint64_t expected_lsn = 2; // Assume LSN 1 was already processed

    // Receive out of order
    quarantine.insert(2, {2});
    quarantine.insert(4, {4});
    quarantine.insert(5, {5});

    std::cout << "Mask after 2, 4, 5: " << std::hex << quarantine.get_presence_mask() << std::dec << "\n";
    // Only LSN 2 is ready. 3 is missing, so 4 and 5 remain quarantined.

    // The gap is filled!
    quarantine.insert(3, {3});

    // flush_ready automatically advances the expected_lsn and cascades through all ready packets
    quarantine.flush_ready([](const NetworkPacket& data, uint64_t sequence) {
        std::cout << "[FLUSH] Processed LSN: " << sequence << "\n";
    });
}
```