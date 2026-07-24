# ADR 001: Rejection of OOP Inheritance and Virtual Dispatch

## Status

Accepted

## Context and Problem Statement

Traditional C++ event-driven frameworks rely heavily on Object-Oriented Programming (OOP) principles, specifically runtime polymorphism. A common pattern is to define a virtual class `BaseEvent` and derive specialized events from it, allowing a handler to process `BaseEvent*` via a vtable lookup.

While this provides high cognitive flexibility for developers, it introduces catastrophic performance penalties in an ultra-low latency environment:

- **Memory Fragmentation:** Derived events have different `sizeof()` footprints. An O(1) slab pool requires uniformly sized blocks.
- **Cache Misses (vptr Bloat):** Every polymorphic object carries a hidden 8-byte `vptr`. In a 64-byte cache line, wasting 8 bytes per event reduces effective memory bandwidth by 12.5%.
- **Instruction Pipeline Stalls:** Virtual function calls require indirect branch prediction. If the CPU mispredicts, it flushes the instruction pipeline, costing 15-20 CPU cycles per event.

## Decision

We explicitly ban the use of virtual functions, RTTI (`dynamic_cast`), and standard OOP inheritance for all hot-path event and handler structures in SLABFLUX.

Instead, we enforce the following compile-time mechanisms:

- **For logic (handlers):** Use the Curiously Recurring Template Pattern (CRTP) for static polymorphism.
- **For dispatching:** Use C++20 Concepts (`requires`) and template fold expressions to resolve routing completely at compile time.
