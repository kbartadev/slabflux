# Blueprint: pool.hpp

## Architectural Overview
Provides strictly bounded, wait-free memory arenas (`spsc_pool`, `mpsc_pool`) that permanently eliminate dynamic heap fragmentation (`malloc` / `new`) on the execution hot path.

## Core Logic & Mechanisms
- **In-Place Construction**: Exposes `make(...)` and `make_raw()` factories utilizing perfect-forwarding to construct objects natively within the pre-allocated slab array block.
- **Automated Reclaim Strategy**: Integrates compile-time bounds and cyclic automatic-reclamation policies (`reclaim_strategy::automatic`) to gracefully self-heal ring exhaustion.