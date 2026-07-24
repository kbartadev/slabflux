# Blueprint: context_vault.hpp

## Architectural Overview
Provides the thread-safe context management facility for the Ephemeral Context Engine, completely bypassing global singletons and thread-local dictionary lookups.

## Core Logic & Mechanisms
- **Tuple-Based Storage**: Consolidates heterogeneous Context objects into a tightly packed `std::tuple`, enforcing cache-aligned (`alignas(64)`) stack residency.
- **Zero-Allocation Lifecycles**: Context structures exist exclusively for the duration of a network dispatch tick and are cleared instantly via `clear_all()` using C++ fold expressions.
- **MOC Annotations**: Exposes the `REGISTER_CONTEXT(EventType, ...)` macro, allowing offline meta-compilers to explicitly map stateless events to stateful contexts before runtime.