# Blueprint: extends.hpp

## Architectural Overview
Provides a 4D Matrix Dispatch mechanism that emulates Object-Oriented inheritance at compile time, eliminating Virtual Method Tables (`vtable`) and cache misses entirely.

## Core Logic & Mechanisms
- **Static Tagging**: The `extends<BaseEvent>` template acts as an explicit compilation marker indicating a parent-child structural relationship between raw C++ POD definitions.
- **Cascading Unrolling**: Instructs the `pipeline` dispatcher to sequentially unpack the hierarchy. It invokes handlers associated with the base layer first, immediately followed by the specific derived handler via continuous inline pointer casts.