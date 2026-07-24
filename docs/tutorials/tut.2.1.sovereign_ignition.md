# Tutorial 2.1: The Sovereign Ignition Phase

## 1. The Principle of Ignition
SlabFlux explicitly separates the lifecycle of a process into two rigid phases: **Ignition** and **Hot Path**. 
During Ignition, dynamic memory allocation (`new`, `malloc`), file descriptor acquisition, and OS-level thread spawning are permitted. Once the `active_environment` seals the binary and transitions to the Hot Path, all kernel-mediated resource acquisition is strictly forbidden.

## 2. Hardware Topology Enforcement (`topology_enforcer.hpp`)
In a NUMA (Non-Uniform Memory Access) architecture, executing a thread on Node 0 while its memory pool resides on Node 1 forces data to cross the QPI/UPI interconnect, adding severe microsecond latency.

The `topology_enforcer` enforces hardware sympathy:
*   **Core Pinning**: Isolates the OS thread to a specific, isolated CPU core using `pthread_setaffinity_np` or equivalent `sched_setaffinity` syscalls.
*   **NUMA Node Binding**: Validates that all associated `spsc_pool` and `context_vault` topologies share the same physical silicon layout as the executing thread.

## 3. The Sovereign Core (`sovereign_core.hpp`)
The `sovereign_core` is the master loop of the Hot Path. It abandons `epoll` or standard event loops. Instead, it enters a zero-blocking, $O(1)$ polling cascade across defined `mpmc_conduit` and `spsc_conduit` queues.

### Hands-On: Bootstrapping the Environment

```cpp
#include "slabflux/rte/active_environment.hpp"
#include "slabflux/sys/topology_enforcer.hpp"
#include "slabflux/core/sovereign_core.hpp"
#include "slabflux/core/pipeline.hpp"
#include <iostream>
#include <thread>

struct DummyHandler {
    void on(int) {}
};

int main() {
    // 1. Initialize the global environment registry
    slabflux::rte::active_environment env;

    // 2. Define the pipeline geometry
    slabflux::core::pipeline<DummyHandler> main_pipeline(DummyHandler{});

    // 3. Instantiate the Sovereign Core linking to the pipeline
    slabflux::core::sovereign_core execution_engine(main_pipeline);

    // 4. Ignite a dedicated OS thread for the Hot Path
    std::thread hot_path_thread([&]() {
        // Enforce NUMA Node 0, Physical Core 2
        slabflux::sys::topology_enforcer::pin_thread(2, 0);
        
        std::cout << "[SYSTEM] Ignition Complete. Sealing environment...\n";
        
        // Enter the infinite, zero-allocation polling state
        execution_engine.execute_sealed_loop();
    });

    hot_path_thread.join();
    return 0;
}
```

## 4. Best Practices
*   **Isolate Cores**: Always use kernel boot parameters (e.g., `isolcpus=2-8` on Linux) to prevent the OS scheduler from interrupting your `sovereign_core` thread.
*   **Seal Validation**: Use static analysis or custom wrappers to ensure no `std::string` formatting or `std::vector` resizing happens after `execute_sealed_loop()` is invoked.