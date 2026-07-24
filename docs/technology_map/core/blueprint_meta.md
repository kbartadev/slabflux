# Blueprint: meta.hpp

## Architectural Overview
Provides the central declarative API for compile-time relationships, bridging raw stateless network event types with their respective mutable state environments.

## Core Logic & Mechanisms
- **Context Typelist Injection**: Defines `event_context_map<Event>`, which the pipeline unroller statically queries during dispatch to instantiate or resolve local stack variables automatically.
- **SFINAE Fallbacks**: Houses the underlying type-traits and `decltype` evaluations to gracefully degrade behavior and ignore context injection if no metadata map is registered for a given structure.