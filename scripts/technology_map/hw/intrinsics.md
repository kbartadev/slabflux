# Blueprint: intrinsics.hpp

## Architectural Overview
Standardizes raw silicon instruction calls across heterogeneous compiler toolchains (GCC, Clang, MSVC) to support branchless mathematical processing.

## Core Logic & Mechanisms
- **Bitwise Unification**: Exposes reliable wrappers for `tzcnt_32`, `popcnt`, and `lzcnt` to guarantee identical single-cycle execution regardless of the host environment.
- **Zero-Branch Math**: Replaces iterative loops and conditional divisions with native CPU instructions to preserve the processor's branch prediction buffers.