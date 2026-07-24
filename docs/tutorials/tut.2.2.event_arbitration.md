# Tutorial 2.2: Event Arbitration & Backpressure

## 1. The Starvation Problem
When a `sovereign_core` polls multiple ingress conduits (e.g., a High-Priority Market Data queue and a Low-Priority Logging queue), a burst of 100,000 market data packets can entirely starve the logging queue. While market data is critical, infinite starvation of side-channels leads to deadlocks and TCP socket buffers overflowing on the OS edge.

## 2. Event Arbitration (`event_arbiter.hpp`)
The `event_arbiter` provides a statically resolved $O(1)$ routing hierarchy. It maintains a strict processing cadence across three primary channels: **Admin**, **Temporal**, and **Data**.

### The 5-Strike Rule
To prevent high-frequency market data from locking out the control plane, the arbiter utilizes a strike counter. If the **Admin** queue is empty during a polling cycle, it increments a strike. After 5 strikes, the arbiter forces an interleaved poll of the Data channel before returning to verify the Admin bus. This guarantees configuration reloads and liveness monitors are never starved.

## 3. PI/PID Flow Control (`flow_controller.hpp` & `backpressure_valve.hpp`)
The `flow_controller` implements a **Hysteresis Loop (80/40 Rule)**. 
1.  **High Watermark (80%)**: Triggers a backpressure signal that pauses the `io_uring_ingress` submission queue.
2.  **Low Watermark (40%)**: Re-arms the ingress once the compute engine has drained the conduit.
This creates upstream backpressure, triggering TCP Window Scaling collapse at the network layer and preventing "Buffer Bloat."

### Hands-On: Managing Priority Conduits

```cpp
#include "slabflux/rte/event_arbiter.hpp"
#include "slabflux/core/pendulum_spsc_conduit.hpp"
#include "slabflux/core/backpressure_valve.hpp"
#include "slabflux/rte/flow_controller.hpp"

struct AdminCmd { int code; };
struct MarketData { double price; };

int main() {
    // Conduits for Admin and Data paths
    slabflux::core::pendulum_spsc_conduit<AdminCmd, 1024> admin_bus;
    slabflux::core::pendulum_spsc_conduit<MarketData, 1024> data_bus;

    // Instantiate Arbiter (Statically prioritizes Admin)
    slabflux::rte::event_arbiter arbiter(5);

    // Connect Flow Controller to Data Bus
    slabflux::core::backpressure_valve<1024, 100> data_valve(819); // 80% of 1024
    slabflux::rte::flow_controller flow_manager;

    // Hot Path Polling Loop
    while (true) {
        // 1. Update Ingress Throttle based on Load Hysteresis
        flow_manager.update();

        // 2. Poll across the hierarchy (Admin -> Time -> Data)
        arbiter.poll(
            [&]() -> AdminCmd* { 
                AdminCmd* p = nullptr; 
                if (admin_bus.try_pop(p)) return p;
                return nullptr;
            },
            [&]() -> MarketData* { 
                MarketData* p = nullptr; 
                if (data_bus.try_pop(p)) return p;
                return nullptr;
            }
        );
        
        // Yield hint if both empty
        asm volatile("pause" ::: "memory");
    }
}
```

## 4. Anti-Patterns
*   **Anti-Pattern: Dynamic Wait/Sleep.** Never use `std::this_thread::sleep_for()` to implement backpressure. Sleeping yields the core to the OS scheduler, imposing minimum latencies of ~50-500 microseconds. Use the `flow_controller` to mathematically scale down fetch batch sizes.