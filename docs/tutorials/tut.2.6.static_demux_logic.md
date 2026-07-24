# Tutorial 20: Compile-Time Tag Demuxing & Transport Decoupling

In ultra-low-latency architectures, data arrives as a raw, untyped byte stream (e.g., ring buffers, network sockets). The engine must identify the packet, cast it to the correct C++ structure, and route it to the execution pipeline with sub-nanosecond overhead.

The `demuxer` pattern provides this routing logic, physically decoupling transport tokens (like `tagged_pointer`) from the core `pipeline`, ensuring strict Single Responsibility Principle (SRP) and zero-cost, branchless dispatch.

## 1. The Architectural Problem

Hardcoding transport-specific parsing (e.g., identifying packet types from network bytes) directly into the execution `pipeline` creates tight coupling. 

**The Solution:** The pipeline must only understand pure, typed C++ events. The `demuxer` acts as a static "bouncer," translating transport tokens into typed dispatches at compile-time.

## 2. The `tagged_pointer` Transport Token

SLABFLUX utilizes a `tagged_pointer`—an 8-byte, cache-friendly register token. It borrows unused bits from a 64-bit memory address to store a type ID (tag), allowing instant retrieval of both the address and the type without vtable lookups or heap-allocated headers.

## 3. The Demuxer Implementation (`demuxer.hpp`)

The `slabflux::core::demuxer` is a pure, static construct that satisfies the `DemuxableEvent` concept. It uses C++20 fold expressions to expand variadic type packs into a sequence of ID comparisons.

Because event IDs are `static constexpr`, the compiler resolves these into optimal machine code—frequently utilizing Jump Tables or `CMOV` (conditional move) instructions to eliminate branch penalties.

This guarantees O(1) routing latency, regardless of how many event types are supported in the bus.

```cpp
#include "slabflux/core/demuxer.hpp"
#include "slabflux/core/pipeline.hpp"

// 1. Definition (ID = 1)
struct limit_order {
    static constexpr uint16_t ID = 1;
    uint64_t instrument_id;
    double price;
};

// 2. Risk Layer Strategy
struct pre_trade_risk {
    inline bool on(limit_order& order) noexcept {
        if (order.price <= 0.01) return false; // Immediate short-circuit
        return true;
    }
};

// 3. Gateway Ingress
int main() {
    pre_trade_risk risk_layer;
    slabflux::core::pipeline<pre_trade_risk> hft_gateway(risk_layer);
    
    // Define the bus
    using ingress_bus = slabflux::core::demuxer<limit_order>;

    // Receive raw packet
    limit_order raw_order{9921, 150.25};
    slabflux::core::tagged_pointer p = slabflux::core::tagged_pointer::pack(limit_order::ID, &raw_order);

    // Route: The demuxer casts and dispatches to the pipeline
    ingress_bus::route(p, hft_gateway);
}
```
