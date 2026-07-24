# Tutorial 02: Deterministic Unrolling & The 7D Dispatcher

## 1. The $N \times M$ Cartesian Unroll

SlabFlux operates on a multi-dimensional execution manifold. The two most prominent axes are:
*   **N (Handler Axis):** The depth of the handler inheritance tree.
*   **M (Event Axis):** The depth of the event inheritance tree.

The SlabFlux dispatcher resolves the complete $N \times M$ Cartesian product of all possible handler/event intersections at **compile time**. It generates a single, flat, branchless instruction sequence. There are no virtual dispatch tables (vtables) or runtime reflection algorithms used in the hot path.

## 2. The Inverse Priority Poset

When parallel tracks (sibling classes in a multiple-inheritance DAG) need tie-breaking, SlabFlux utilizes an **Inverse Priority Scale** to avoid runtime memory-index flipping.

- The lowest numerical value represents the absolute highest processing precedence.
- **Leaf Precedence:** The concrete, terminal leaf node of an event or handler implicitly occupies **Rank 0**. It requires no token.

### Resolving Diamond Inheritance

Consider a standard C++ diamond DAG where $D$ inherits from $B$ and $C$, and both inherit from $A$. If the dispatcher attempts to linearize this without priorities, it will trigger a `static_assert` due to topological ambiguity.

We fix this by injecting `slabflux::priority<N>`.

```cpp
class EventA {};

// EventB is given priority 10 (higher precedence than EventC)
class EventB : public EventA {
public:
    using slabflux::priority<10>; 
};

// EventC is given priority 20 (lower precedence than EventB)
class EventC : public EventA {
public:
    using slabflux::priority<20>;
};

// EventD is the leaf. It is implicitly Rank 0.
class EventD : public EventB, public EventC {};
```

## 3. Dissecting the Unrolled Assembly

If you register a handler capable of observing all four event interfaces and push `EventD`, the dispatcher guarantees the base $A$ is executed exactly once, followed by the branches according to inverse priority.

```cpp
struct DiamondHandler {
    bool on(EventA&) { return false; }
    bool on(EventB&) { return false; }
    bool on(EventC&) { return false; }
    bool on(EventD&) { return false; }
};

int main() {
    slabflux::pipeline<DiamondHandler> pipe(DiamondHandler{});
    EventD leaf_event;
    
    pipe.dispatch(leaf_event);
}
```

**What the Compiler Generates:**

The above $1 \times 4$ topological projection is flattened statically into identical machine code equivalent to:

```cpp
// 1. Base Event (Guard)
if (handler.on(static_cast<EventA&>(leaf_event))) return;

// 2. Priority 10 branch
if (handler.on(static_cast<EventB&>(leaf_event))) return;

// 3. Priority 20 branch
if (handler.on(static_cast<EventC&>(leaf_event))) return;

// 4. Terminal Leaf (Implicit Rank 0)
if (handler.on(leaf_event)) return;
```