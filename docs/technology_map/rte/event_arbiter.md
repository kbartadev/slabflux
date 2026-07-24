# SlabFlux RTE: Event Arbiter (`event_arbiter.hpp`)
# Event Arbiter (`event_arbiter.hpp`)

## 1. Architectural Overview
Traditional event loops use heap-allocated Priority Queues (e.g., `std::priority_queue`) that invoke logarithmic `O(log N)` sorting on every push and pop. The `event_arbiter` replaces this with a statically resolved, metaprogrammed `O(1)` routing hierarchy.
## Architekturális Koncepció
Többcsatornás, statikusan feloldott prioritásos Scheduler (Ütemező). Külön Conduitokon kezeli az Admin/Control, a Temporal (Tick) és a Data (Nyers Forgalom) eseményeket.

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
## Szinkronizációs Modell
A rendszer szigorú elágazásmentes (branchless) nyomkövetést (polling trace) valósít meg. A Data csatornát folyamatosan olvassa, de a vezérlő (Admin) csatorna prioritását garantálja. 
**Starvation Prevention (Kiéheztetés elleni védelem):** Beépített statikus "Strike" számláló. Ha az alacsony prioritású csatornán sok esemény érkezik, 5 ciklus után kikényszerítetten visszatér az Admin csatorna olvasásához.