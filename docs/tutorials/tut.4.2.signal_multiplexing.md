# Tutorial 4.2: Hardware Signal Multiplexing

## 1. The Fan-Out Bottleneck
In heavily instrumented execution manifolds, a single event (e.g., a completed trade) must often be broadcast simultaneously to multiple hardware boundaries: written to an SSD via `io_uring`, sent to a distributed mesh via `network_conduit`, and forwarded to a GPU telemetric cell.

Standard loop-based iterations (`std::vector<Handler*>::iterator`) introduce runtime memory indirection, cache misses, and pipeline stalling. 

## 2. Compile-Time Unrolling (`signal_multiplexer.hpp`)
The `signal_multiplexer` solves this by utilizing C++17 Fold Expressions. It allows you to bind an arbitrary number of distinct hardware targets into a single struct. 

When the multiplexer invokes the broadcast, the C++ compiler unrolls the variadic parameter pack into sequential, flat machine code. 

### Hands-On: Unrolling Pointer Broadcasts

```cpp
#include "slabflux/hw/signal_multiplexer.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include <cstdint>

// Hardware Boundary 1: A network edge conduit
struct NetworkBoundary {
    void transmit(uint64_t state_id) { /* __atomic_store to network ring */ }
};

// Hardware Boundary 2: A durable storage conduit
struct StorageBoundary {
    void transmit(uint64_t state_id) { /* __atomic_store to io_uring ring */ }
};

// The Multiplexer utilizing Variadic Templates
template <typename... Boundaries>
struct HardwareMultiplexer {
    std::tuple<Boundaries*...> targets;

    HardwareMultiplexer(Boundaries*... args) : targets(std::make_tuple(args...)) {}

    void broadcast(uint64_t state_id) {
        // C++17 Fold Expression over the tuple elements.
        // This physically generates zero loops at runtime.
        std::apply([state_id](auto*... boundary) {
            (..., boundary->transmit(state_id));
        }, targets);
    }
};

int main() {
    NetworkBoundary net;
    StorageBoundary disk;

    // Instantiate the multiplexer
    HardwareMultiplexer<NetworkBoundary, StorageBoundary> mux(&net, &disk);

    // Emits the signal. The compiler generates: net.transmit(101); disk.transmit(101);
    mux.broadcast(101);
}
```