# Blueprint: socket_duplex.hpp

## Architectural Overview
Consolidates standard `send` and `recv` POSIX capabilities into a singular engine poll sequence, minimizing context-switching penalties.

## Core Logic & Mechanisms
- **Loop Symmetry**: Resolves both reading loops and writing operations consecutively in one execution frame.
- **Contention Erasure**: Centralizes thread execution over the file descriptor to eliminate mutex overhead inherent in multi-threaded socket operations.