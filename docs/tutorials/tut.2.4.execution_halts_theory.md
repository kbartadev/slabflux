# Tutorial 07: Execution Halts & Circuit Arbitration

## 1. The Necessity of Deterministic Truncation

In high-frequency trading (HFT) and mission-critical distributed systems, processing malformed data or violating risk parameters is catastrophic. 

Because the SlabFlux Dispatcher flattens the execution DAG ($N \times M$ Cartesian unroll) at compile time, we need a zero-overhead mechanism to stop the execution sequence instantly if a base-level validation fails. We call this **Circuit Arbitration** or **Execution Halts**.

## 2. Boolean Arbitration Topology

Handlers inside a `slabflux::pipeline` are not required to return `void`. If a handler returns `bool`, the dispatcher inherently treats this as a truncation signal:
- Returning `false` means **continue** executing the pipeline.
- Returning `true` means **halt** propagation entirely.

Because SlabFlux executes in a **leaf-first, top-down** sequence (Guard-Before-Action), base handlers (acting as guards) are always evaluated before specialized leaf handlers.

## 3. Hands-On: Building a Pre-Trade Risk Guard

Let's define a topology where a base event represents a market trade. A risk handler will act as a circuit breaker.

```cpp
#include "slabflux/core/pipeline.hpp"
#include <iostream>

// 1. Base Event
struct TradeInstruction {
    double notional_value;
    bool is_compliant;
};

// 2. Derived Specific Event
struct EquityTrade : public TradeInstruction {
    char ticker[8];
};

// 3. The Guard Handler (Evaluated First)
struct RiskArbiter {
    // Returns bool: True = Halt, False = Continue
    bool on(TradeInstruction& e) {
        if (e.notional_value > 100'000.0) {
            std::cout << "[REJECTED] Risk limit exceeded.\n";
            return true; // HALT pipeline here.
        }
        
        if (!e.is_compliant) {
            std::cout << "[REJECTED] Compliance check failed.\n";
            return true; // HALT pipeline here.
        }
        
        return false; // Proceed to derived handlers
    }
};

// 4. The Execution Handler (Evaluated Last)
struct ExecutionEngine {
    // Returning void means this handler cannot halt the pipeline
    void on(EquityTrade& e) {
        // This code physically cannot be reached if the RiskArbiter returns true
        std::cout << "[ACCEPTED] Sending to exchange.\n";
    }
};
```

### Dispatching the Unrolled Graph

When we compile a pipeline containing both the `RiskArbiter` and the `ExecutionEngine`, the dispatcher physically generates the following branch logic:

```cpp
slabflux::core::pipeline<RiskArbiter, ExecutionEngine> pipe(arbiter, engine);

// Inside pipe.dispatch(my_trade):
// if (arbiter.on(static_cast<TradeInstruction&>(my_trade))) return;
// engine.on(my_trade);
```

The `ExecutionEngine` operates on a mathematically guaranteed valid state. It requires zero internal `if` statements regarding compliance or risk bounds.