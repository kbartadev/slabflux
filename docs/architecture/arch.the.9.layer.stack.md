# The 8-Layer Stack Architecture

SLABFLUX is modeled as a physical hardware stack rather than a software library.

## Physical Layers

### Layer 1: Ownership

`event_ptr` manages unique ownership and O(1) return to the pool.

### Layer 2: Slab Pool

Union-based O(1) deterministic allocator.

### Layer 3: Conduit

Padded SPSC ring buffer for lock-free cross-thread flux.

### Layer 4: Cluster

The spatial routing boundary for physical tracks.

## Topological and Logical Layers

### Layer 5: Switches and Pollers

Deterministic Fan-Out and Fan-In logic.

### Layer 6: Pipeline

Sequential CRTP-based logic execution via C++20 Concepts.

### Layer 7: Sink

The bridge between a physical conduit and a logical pipeline.

### Layer 8: External Boundaries

Zero-copy reconstruction of network byte streams.

### Layer 9: Runtime Domain & Routing

The highest abstraction layer that orchestrates multiple processing units and manages the system topology.
