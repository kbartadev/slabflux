# Blueprint: context_vault.hpp

## Architectural Overview
The `context_vault.hpp` header defines the framework's ephemeral state architecture. It substitutes costly, heap-allocated dictionary lookups with continuous, stack-bound memory segments that exist strictly for the duration of a single tick.

## Core Logic & Mechanisms
- **Context Vault (`context_vault`)**: A strictly cache-aligned (`alignas(64)`) heterogeneous memory layout (`std::tuple<Contexts...>`). It guarantees that context properties utilized by different pipeline stages remain permanently hot in the L1 CPU cache.
- **MOC Registration Macro (`REGISTER_CONTEXT`)**: Provides the static annotation syntax `REGISTER_CONTEXT(Event, Context)` consumed by the offline `slabflux_meta` compiler to map structural events to their respective mutable states.
- **O(1) State Clearing (`clear_all`)**: Exposes highly optimized fold expressions to instantly zero-out or destruct context structures at the start of a network burst, guaranteeing zero state-leakage between discrete event processing cycles.