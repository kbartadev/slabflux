# Tutorial 16: Chaos Engineering and Fault Injection

When building systems that must survive hardware failures, network partitions, and malicious payloads, standard unit testing is not enough. You must actively break the system in testing environments to prove its resilience.

SLABFLUX provides the `chaos_engine` to deterministically inject catastrophic failures into your pipelines to verify that memory does not leak and states do not corrupt.

## 1. Injecting Faults into the Conduit

You can wrap any `spsc_conduit` with the chaos engine. It will randomly drop packets, duplicate events, or inject artificially malformed memory addresses based on a deterministic seed.

```cpp
#include "slabflux/supplemental/chaos_engine.hpp"
#include "slabflux/core/conduit.hpp"
#include <iostream>

void run_chaos_simulation() {
    slabflux::core::spsc_conduit<network_packet*, 1024> clean_bus;
    
    // Attach the Chaos Engine. 
    // We configure a 5% packet drop rate and a 1% duplication rate.
    slabflux::supplemental::chaos_engine<network_packet*> chaos_bus(clean_bus);
    chaos_bus.set_drop_probability(0.05);
    chaos_bus.set_duplicate_probability(0.01);

    // Producer sends 10,000 packets
    for(int i = 0; i < 10000; i++) {
        auto* packet = pool.make(i);
        chaos_bus.push(packet); // 5% of these will simply vanish
    }

    // Consumer reads from the bus.
    // The developer MUST ensure the logic can handle sequence gaps and duplicates gracefully!
    int packets_received = 0;
    while (auto* ev = chaos_bus.pop()) {
        packets_received++;
        pool.release(ev);
    }
    
    std::cout << "Chaos Test Complete. Expected 10,000 packets, received: " << packets_received << "\n";
}
```
