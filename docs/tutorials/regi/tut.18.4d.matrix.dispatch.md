# Tutorial 18: The 4D Matrix Dispatch (Compile-Time Inheritance)

While SLABFLUX bans *Virtual Object-Oriented Inheritance* due to `vptr` bloat and cache misses, there are scenarios where hierarchical logic is genuinely useful (e.g., executing base "Network Packet" logic before executing specific "Player Move" logic).

To support this without virtual functions, SLABFLUX provides the `extends<T>` metaprogramming utility. This creates a **4D Straight-Line Matrix**: a compile-time hierarchy that allows the pipeline to trigger base handlers and derived handlers sequentially, with 100% inlining and zero runtime overhead.

## 1. Static Inheritance using `extends<T>`

Instead of using standard C++ inheritance, derived events use `extends<BaseEvent>`. This registers the relationship in the framework's metaprogramming engine.

```cpp
#include "slabflux/core.hpp"

// The Base Event
struct base_network_packet {
    uint64_t timestamp;
};

// The Derived Event explicitly extends the base using the framework's metaprogramming type
struct trade_execution : slabflux::core::extends<base_network_packet> {
    double execution_price;
};
```

## 2. Matrix Handlers

You can write independent handlers for the Base event and the Derived event. The Matrix Pipeline is intelligent enough to traverse the hierarchy and invoke everything in the correct order.

```cpp
#include <iostream>
#include "slabflux/core/pipeline.hpp"

struct latency_monitor {
    // Only cares about the base packet
    void on(const base_network_packet& ev) {
        std::cout << "[Monitor] Packet received at: " << ev.timestamp << "\n";
    }
};

struct trade_logic {
    // Only cares about the specific trade event
    void on(const trade_execution* ev) {
        std::cout << "[Trade] Executing at price: " << ev.execution_price << "\n";
    }
};
```

## 3. The 4D Cascade in Action

When you dispatch the derived event, the pipeline uses C++17 Fold Expressions to unpack the inheritance tree. It automatically casts the pointer and routes it to the base handler first, then to the derived handler. 

```cpp
int main() {
    latency_monitor monitor;
    trade_logic trade;
    
    slabflux::core::pipeline matrix(monitor, trade);

    // Create the derived event
    trade_execution exec_ev;
    exec_ev.timestamp = 1682390400;
    exec_ev.execution_price = 45000.50;

    // Dispatching the derived event triggers a 4D cascade:
    // 1. matrix calls monitor.on( (base_network_packet*)&exec_ev )
    // 2. matrix calls trade.on( &exec_ev )
    matrix.dispatch(&exec_ev);

    return 0;
}
```
