# Blueprint: memory.hpp

## Architectural Overview
Serves as the unified abstraction layer for core engine memory primitives, governing explicit ownership wrappers and lightweight transport tokens.

## Core Logic & Mechanisms
- **Tagged Pointers**: Fuses a 16-bit type ID and a physical memory address into a single 64-bit integer payload, permitting O(1) demuxing resolution without virtual inheritance headers.
- **Scoped Pointers**: Enables explicit ownership-stealing semantics inside the pipeline, dynamically triggering downstream short-circuits to avoid use-after-free corruption while moving pointers across thread lines.