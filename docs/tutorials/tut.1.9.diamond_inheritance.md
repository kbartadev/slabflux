# Tutorial 1.9: Diamond Inheritance & Static Linearization

In high-performance C++, Diamond Inheritance is usually avoided due to the overhead of virtual base classes. In SlabFlux, the `pipeline` dispatcher solves the diamond problem using **Topological Linearization** at compile time, ensuring base handlers are executed exactly once without using `virtual`.

## 1. Topological Sorting (`meta.hpp`)
The dispatcher uses `slabflux::typelist` algebra to expand the inheritance tree of an event. If it detects a diamond (e.g., $D \to B,C \to A$), it calculates a strict execution vector.

## 2. Resolving Ambiguity with `priority`
When two siblings (like $B$ and $C$) exist at the same depth, you must provide a tie-breaker using `slabflux::priority<N>` from `meta.hpp`. A lower numerical value has higher precedence (Inverse Priority).

```cpp
#include "slabflux/core/meta.hpp"
#include "slabflux/core/pipeline.hpp"

struct Root { int id; };
struct BranchAlpha : public Root { using priority = slabflux::priority<10>; };
struct BranchBeta  : public Root { using priority = slabflux::priority<20>; };
struct Leaf : public BranchAlpha, public BranchBeta {};

struct DiamondHandler {
    // Executed exactly once for a 'Leaf' event
    bool on(Root& r) { return r.id < 0; }
    
    // Executed second (Priority 10)
    void on(BranchAlpha& a) { /* ... */ }
    
    // Executed third (Priority 20)
    void on(BranchBeta& b) { /* ... */ }
    
    // Executed last
    void on(Leaf& l) { /* ... */ }
};
```

## 3. Dissecting the Assembly
The compiler unrolls this diamond into a flat call sequence:
1. `handler.on(static_cast<Root&>(event))`
2. `handler.on(static_cast<BranchAlpha&>(event))`
3. `handler.on(static_cast<BranchBeta&>(event))`
4. `handler.on(event)`

Because this is resolved at compile time, there are zero `dynamic_cast` calls or vtable jumps.

## 4. Best Practices
*   **Avoid State Redundancy**: Ensure that common state in `Root` is only modified once to prevent logic corruption during the unrolled sequence.
*   **Implicit Leaf Rank**: Terminal leaf nodes are implicitly **Rank 0**. You do not need to provide a priority token for the leaf itself.

---
**Next Steps:** Proceed to **Tutorial 2.1** to learn how to ignite the engine and pin these pipelines to physical hardware cores.
