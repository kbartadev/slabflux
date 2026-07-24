# Tutorial 1.5: Memory Topologies & Context Vaults

## 1. The Parameter Bloat Problem

In traditional message-passing architectures, providing downstream handlers with environment state (e.g., configuration variables, connection pools, or static market data) usually involves threading context pointers through every function signature in the pipeline. 

This creates "parameter bloat." It causes unnecessary register pressure, destroys ABI cleanliness, and degrades the deterministic $N \times M$ compile-time unrolling of the dispatcher.

## 2. Zero-Cost Injection via Context Vaults

SlabFlux solves this with the `slabflux::core::context_vault<Args...>`. A vault is a variadic template that physically embeds context objects into a contiguous, cache-aligned memory layout.

Instead of manual pointer threading or macros, handlers request context by providing a signature match in their `on()` method. The `pipeline` dispatcher utilizes SFINAE probing at compile time to identify handlers with the signature `on(Event&, Context&)` and automatically injects the appropriate reference from the vault. This resolution is a pure memory offset computation with zero runtime indirection.

## 3. Hands-On: Contextual Dispatch

```cpp
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// 1. Define the cache-friendly environment state
struct alignas(64) OrderBookVault {
    double current_bid;
    double current_ask;
};

struct OrderEvent { double price; };

// 2. The Handler implementation
struct ExecutionHandler {
    // The dispatcher automatically resolves 'OrderBookVault' from the vault
    void on(OrderEvent& e, OrderBookVault& book) {
        if (e.price >= book.current_ask) {
            // execute_trade(e);
        }
    }
};

int main() {
    ExecutionHandler handler;
    slabflux::core::pipeline<ExecutionHandler> pipe(handler);

    // 3. Instantiate the contiguous context vault
    slabflux::core::context_vault<OrderBookVault> vault;
    vault.get<OrderBookVault>().current_ask = 150.75;

    OrderEvent event{ 151.00 };
    // 4. Pass the vault as the second argument to dispatch()
    pipe.dispatch(event, vault);
}
```

## 4. Best Practices & Anti-Patterns

*   **Best Practice: L1 Cache Alignment.** Always use `alignas(64)` on your vault structures to ensure they map perfectly to x86-64 L1 cache lines. This physically prevents False Sharing when multiple sovereign cores operate in parallel.
*   **Best Practice: Immutability.** Use `const` modifiers on vaults whenever the state is strictly read-only for that specific execution thread to hint the compiler to aggressively cache the values in registers.
*   **Anti-Pattern: Dynamic Pointers.** Never store `std::shared_ptr` or raw dynamic memory pointers inside a context vault if they require dereferencing in the hot path. The vault itself should contain the raw, contiguous, geometric state. Memory fragmentation breaks hardware sympathy.