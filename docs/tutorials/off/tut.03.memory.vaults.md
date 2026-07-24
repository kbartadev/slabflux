# Tutorial 03: Memory Topologies & Context Vaults

## 1. The Parameter Bloat Problem

In traditional message-passing architectures, providing downstream handlers with environment state (e.g., configuration variables, connection pools, or static market data) usually involves threading context pointers through every function signature in the pipeline. 

This creates "parameter bloat." It causes unnecessary register pressure, destroys ABI cleanliness, and degrades the deterministic $N \times M$ compile-time unrolling of the dispatcher.

## 2. Zero-Cost Injection via Context Vaults

SlabFlux solves this with **Context Vaults**. A vault is a statically allocated, cache-aligned memory topology bound implicitly to the execution lane of the pipeline.

Handlers do not receive environmental state via function arguments. Instead, they extract it using the `REGISTER_CONTEXT` macro. Because the execution manifold maps pipeline instances to specific hardware lanes, this context resolution is reduced to a pure memory offset computation—there is zero runtime pointer chasing.

## 3. Hands-On: Using `REGISTER_CONTEXT`

Let's bind an Order Book state directly into a specific execution handler.

```cpp
#include "slabflux/core/context.hpp"

// 1. Define the strictly sized, cache-friendly environment state
struct alignas(64) OrderBookVault {
    double current_bid;
    double current_ask;
    uint64_t timestamp;
};

// 2. The Handler implementation
struct ExecutionHandler {
    // Macro generates a compile-time binding to the vault topology
    REGISTER_CONTEXT(OrderBookVault, book_ctx);

    void on(OrderEvent& e) {
        // book_ctx() returns a structural reference mapped at compile time.
        // No pointer indirection, no virtual tables, zero overhead.
        if (e.price >= book_ctx().current_ask) {
            execute_trade(e);
        }
    }
};
```

## 4. Best Practices & Anti-Patterns

*   **Best Practice: L1 Cache Alignment.** Always use `alignas(64)` on your vault structures to ensure they map perfectly to x86-64 L1 cache lines. This physically prevents False Sharing when multiple sovereign cores operate in parallel.
*   **Best Practice: Immutability.** Use `const` modifiers on vaults whenever the state is strictly read-only for that specific execution thread to hint the compiler to aggressively cache the values in registers.
*   **Anti-Pattern: Dynamic Pointers.** Never store `std::shared_ptr` or raw dynamic memory pointers inside a context vault if they require dereferencing in the hot path. The vault itself should contain the raw, contiguous, geometric state. Memory fragmentation breaks hardware sympathy.