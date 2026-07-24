# Tutorial 6.1: Deterministic Fault Injection

## 1. Validating Resilience
Testing a highly deterministic, $O(1)$ latency system requires deterministic failures. Traditional unit tests cannot easily simulate spontaneous hardware drops, cache-line corruption, or split-brain network partitions synchronously on the hot path.

## 2. The Chaos Engine (`chaos_engine.hpp`)
Residing in the `supplemental/` subsystem, the `chaos_engine` is a testing architecture designed for strict fault injection. It interfaces directly with the routing matrices (like `round_robin_switch` and `mpmc_conduit`).

Instead of relying on pseudo-random sleep threads that disrupt reproducibility, the Chaos Engine utilizes the `deterministic_rng` (from the `compute/` subsystem) to inject failures at exact Logical Sequence Numbers (LSNs).

## 3. Simulating Catastrophe
The Chaos Engine can be programmed to simulate specific conditions:
*   **Conduit Saturation:** Artificially triggering the `backpressure_valve` to test the `flow_controller` PID loops.
*   **Phase Tag Corruption:** Flipping bits in an `mpmc_conduit` phase tag to verify that the Sovereign Core safely stalls and requests re-transmission instead of reading corrupted payload data.

### Hands-On: Intercepting the Pipeline

```cpp
#include "slabflux/supplemental/chaos_engine.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/compute/deterministic_rng.hpp"
#include <iostream>

int main() {
    slabflux::core::mpmc_conduit<int, 1024> network_queue;
    
    // Instantiate the RNG with a fixed seed for 100% reproducible test runs
    slabflux::compute::deterministic_rng rng(0xCAFEBABE);
    
    // Instantiate the chaos engine bridging the RNG and the target conduit
    slabflux::supplemental::chaos_engine chaos(rng);
    
    // Program the chaos engine: Drop 1 out of every 100 packets deterministically
    chaos.configure_drop_rate(0.01);
    chaos.attach_target(&network_queue);

    // Simulate ingress
    for (int i = 0; i < 200; ++i) {
        int* payload = new int(i);
        
        // The chaos engine intercepts the push.
        // At exact deterministic intervals (driven by the RNG), it will discard the payload
        // and return false, simulating a saturated or faulty queue.
        bool success = chaos.try_push_intercept(payload);
        if (!success) {
            std::cout << "[CHAOS] Fault Injected. Payload dropped at iteration: " << i << "\n";
        }
    }
}
```