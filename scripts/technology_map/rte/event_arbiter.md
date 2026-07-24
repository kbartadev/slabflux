# SlabFlux RTE: Event Arbiter (`event_arbiter.hpp`)

## 1. Architectural Overview
Traditional event loops use heap-allocated Priority Queues (e.g., `std::priority_queue`) that invoke logarithmic `O(log N)` sorting on every push and pop. The `event_arbiter` replaces this with a statically resolved, metaprogrammed `O(1)` routing hierarchy.

## 2. Synthesized Polling Trace
The arbiter directly wraps the physical lock-free SPSC Conduits for Admin, Time, and Data.
During execution, it polls the conduits in strict hierarchical order:
1. **Admin / Control** (Highest Priority)
2. **Temporal Ticks** (High Priority)
3. **Network Data** (Standard Priority)

## 3. Starvation Prevention (The 5-Strike Rule)
To prevent a barrage of network data packets from locking out the control plane:
- If the Admin queue remains empty during a read, the `admin_empty_strike_` counter increments.
- Network Data frames are conditionally blocked from being processed unless the strike counter exceeds 5.
- This enforces an interleaved processing cadence, guaranteeing that configuration reloads and liveness monitors are never starved by high-frequency market data spikes.