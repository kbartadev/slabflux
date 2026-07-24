# Blueprint: Ephemeral State Architecture

## Architectural Overview
Contexts provide stateful mutability to otherwise stateless handlers. Instead of attaching transient variables to network events or executing costly hash-map lookups, the engine seamlessly injects strictly scoped reference vaults based on compile-time configurations.

## Core Components
- **Ephemeral Injection (`context.hpp`, `context_vault.hpp`)**: Connects pure payload events to stateful properties using `event_context_map`. A continuous `context_vault` allocated explicitly on the execution thread stack provides localized memory that perishes seamlessly upon logic-cycle completion.
- **Node State Engine (`sf_node_ctx.hpp`)**: The authoritative clock of the logic layer. Manages global Log Sequence Number (LSN) reservations and tracks physical hardware sockets across peer connections to ensure globally ordered, side-effect-free execution barriers.