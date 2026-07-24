# Blueprint: bridge_sync.hpp

## Architectural Overview
The primary synchronization anchor for Bimodal Execution boundaries, moving memory ownership flawlessly across thread contexts without POSIX locks.

## Core Logic & Mechanisms
- **O(1) Boundary Handoff**: Transfers bare pointers allocated from the high-velocity hot path directly into background domains.
- **RAII Deletion Proxies**: Implements `consume()`, automatically returning payload structures to the specific origin `spsc_pool` when the cold-path processing logically terminates.