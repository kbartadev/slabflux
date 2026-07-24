# Tutorial 1.2: Deterministic Dispatch & Context Vaults

## 1. Cartesian Dispatch (`pipeline.hpp`)
The `pipeline` is a 7D Cartesian Dispatcher. It evaluates event propagation paths at compile time, eliminating all `dynamic_cast` and virtual function table (vtable) overhead.

*   **Topological Unrolling**: Using Typelist Algebra (`get_ancestors`, `topological_sort`), the compiler builds an ordered graph of how handlers execute.
*   **Leaf-First Ordering**: Base contexts are processed sequentially.
*   **SFINAE Probing**: The pipeline dynamically probes if a handler implements `on(Event&, Context&)` versus `on(Event*)`. Unmatched events are safely bypassed.

## 2. The Context Vault (`context_vault.hpp`)
In advanced pipelines, handlers require shared state (e.g., configurations, static routing tables). Passing these as arguments bloats signatures. 

The `context_vault` is a variadic template `context_vault<Ctx1, Ctx2, ...>` that physically embeds all required context objects into a contiguous memory layout for perfect L1/L2 cache residency.

### O(1) Memory Offset Extraction
Accessing a context via `vault.get<Ctx>()` is resolved entirely at compile time. It evaluates to a pure memory offset computation—there is zero runtime pointer chasing.

## 3. Hands-On: SFINAE Injection and Hardware Halts

If a handler's `on()` method returns `bool`, the pipeline monitors the outcome (`is_halted(e)`). Returning `true` deterministically short-circuits the rest of the unrolled assembly block.

```cpp
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

struct SecurityContext {
    bool trading_enabled = true;
};

struct OrderEvent {
    double quantity;
};

struct RiskGuard {
    // Request the vault context via SFINAE signature match
    bool on(OrderEvent& e, SecurityContext& ctx) {
        if (!ctx.trading_enabled || e.quantity > 1000) {
            return true; // Hardware Halt: Short-circuits the pipeline
        }
        return false; // Proceed
    }
};

struct ExecutionEngine {
    // ExecutionEngine does not need the Context, SFINAE drops it.
    void on(OrderEvent& e) {
        // Reached only if RiskGuard returned false.
    }
};

int main() {
    RiskGuard guard;
    ExecutionEngine engine;
    
    // Instantiate the contiguous context vault
    slabflux::core::context_vault<SecurityContext> vault;
    
    // The Cartesian Dispatcher
    slabflux::core::pipeline<RiskGuard, ExecutionEngine> pipe(guard, engine);
    
    OrderEvent order{500};
    pipe.dispatch(order, vault);
}
```