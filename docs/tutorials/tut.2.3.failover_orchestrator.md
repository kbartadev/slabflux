# Tutorial 2.3: Cluster Failover Orchestration

## 1. High-Availability (HA) Cluster States
In mission-critical infrastructure, a physical server crash or NIC failure cannot cause systemic downtime. SlabFlux mandates a deterministic Active-Passive layout managed by the `failover_orchestrator`.

Role assignment is mapped through the `state_machine.hpp` module, ensuring transitions are mathematically exhaustive and strictly validated.
- `ACTIVE`: The node is the Sovereign. It processes ingress, modifies vaults, and emits execution signals (e.g., market orders).
- `PASSIVE`: The node receives input, maintains identical topological memory state, but **short-circuits** any external I/O emission.
- `FAULT`: The node detected an unrecoverable invariant failure and has severed its own network ties to prevent cluster contamination.

## 2. Preventing Split-Brain via Nexus Locks
If the network interconnect between the Active and Passive node drops, both might assume the other is dead, resulting in a Split-Brain scenario (two Sovereigns). 

The `failover_orchestrator` mitigates this using Nexus Locks (typically an external hardware tie-breaker, specific cluster quorum, or specialized multicast lease-timing logic). A node may only transition to `ACTIVE` if it provably holds the Nexus Lock.

## 3. Instantaneous Handoffs
When the Active node faults, the Passive node transitions instantly. Because the Passive node maintains an identical `context_vault` layout, no application restart or state-rebuilding is necessary. The `failover_orchestrator` immediately authorizes the `pipeline` to begin emitting physical I/O.

### Hands-On: Managing the State Machine

```cpp
#include "slabflux/core/failover_orchestrator.hpp"
#include "slabflux/workflow/state_machine.hpp"
#include "slabflux/core/pipeline.hpp"
#include <iostream>

struct NetworkEmissionHandler {
    slabflux::core::failover_orchestrator* orchestrator;

    void on(int packet) {
        // Guard: Only emit physical packets if this node is the Sovereign
        if (orchestrator->current_state() != slabflux::workflow::cluster_state::ACTIVE) {
            return; 
        }
        
        std::cout << "[ACTIVE] Emitting packet to network: " << packet << "\n";
    }
};

int main() {
    // 1. Initialize the state machine starting in PASSIVE
    slabflux::workflow::state_machine cluster_fsm(slabflux::workflow::cluster_state::PASSIVE);
    
    // 2. Bind the FSM to the orchestrator
    slabflux::core::failover_orchestrator orchestrator(cluster_fsm);
    
    NetworkEmissionHandler handler{&orchestrator};
    slabflux::core::pipeline<NetworkEmissionHandler> pipe(handler);
    
    // Packet received while PASSIVE - state mutates, but no emission occurs
    pipe.dispatch(1); 
    
    // 3. Heartbeat from Active Node lost. Secure the Nexus Lock and promote.
    orchestrator.promote_to_active();
    
    // Packet received while ACTIVE - physical emission authorized
    pipe.dispatch(2);
}
```