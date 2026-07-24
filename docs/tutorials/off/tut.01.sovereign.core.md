# Tutorial 01: The Sovereign Core & In-Place Topologies

## 1. Architectural Concept: The Axiom of In-Place Identity

In standard object-oriented programming or event-driven systems, data payloads are often moved, copied into queues, or dynamically cast (`dynamic_cast<T>`) at runtime. SlabFlux completely prohibits this.

The core principle of the SlabFlux Dispatcher is the **Axiom of In-Place Identity**. When an event enters the system, its memory address, referential identity, and `const`-qualification remain structurally immutable. 

The dispatcher never copies the event. Instead, it creates **type projections** (compile-time `static_cast` views) of the same underlying object across its base interfaces.

## 2. Guard-Before-Action Topology

SlabFlux dictates a strict **leaf-first, top-down** execution sequence. This means the system executes validation on the deepest base classes before allowing specialized derived logic to touch the event.

### Example Topology

```cpp
struct BaseEvent { 
    int target_exchange_id; 
};
struct DerivedOrder : public BaseEvent { 
    double price; 
};

struct OrderHandler {
    // Base invariant check. Executed FIRST.
    bool on(BaseEvent& e) {
        if (e.target_exchange_id <= 0) return true; // Halt propagation
        return false; // Continue
    }

    // Specialized execution. Executed LAST.
    void on(DerivedOrder& e) {
        // Operates on a mathematically guaranteed valid state
        submit_to_exchange(e.price);
    }
};
```

## 3. Hands-On: Building and Dispatching

A Pipeline is explicitly constructed by providing the handler axis.

```cpp
#include "slabflux/core/pipeline.hpp"

int main() {
    // 1. Instantiate the handler
    OrderHandler handler;

    // 2. Build the unrolled pipeline at compile-time
    slabflux::pipeline<OrderHandler> pipe(handler);

    // 3. Create the event
    DerivedOrder my_order{ .target_exchange_id = 1, .price = 150.50 };

    // 4. Dispatch. 
    // The dispatcher physically unrolls this to:
    // if (handler.on(static_cast<BaseEvent&>(my_order))) return;
    // handler.on(my_order);
    pipe.dispatch(my_order);
}
```