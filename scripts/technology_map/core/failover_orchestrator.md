# SlabFlux Core: Failover Orchestrator (`failover_orchestrator.hpp`)

## 1. Architectural Overview
The `failover_orchestrator` is the central nervous system of SlabFlux's High-Availability (HA) cluster. It is a strictly deterministic, zero-allocation state machine that monitors cluster health and coordinates instantaneous role transitions (Active vs. Passive) without human intervention.

## 2. Split-Brain Prevention
In distributed systems, a network partition can cause both nodes to assume they are the "Active" primary, resulting in diverging states and corrupted trade submissions (Split-Brain).

### Fused Nexus Node Integration
The orchestrator prevents this using a 3-node Quorum strategy utilizing a `fused_nexus_node` (an independent, lightweight arbiter). 
- A passive node will only attempt to seize the Active role if it loses the hardware UDP heartbeat from the Primary *and* successfully acquires a cryptographic lock from the Nexus.
- If the Primary node is alive but isolated from the network, it will fail to renew its lock with the Nexus and deterministically demote itself to Passive, mathematically guaranteeing only one Active node ever exists.

## 3. Microsecond Network Handoff
Traditional failovers rely on BGP/OSPF routing updates, which can take seconds to converge. SlabFlux requires failovers measured in microseconds.

### Gratuitous ARP Spoofing
When the orchestrator commands a node to become Active, the node immediately injects a burst of Gratuitous ARP (Address Resolution Protocol) packets into the Top-of-Rack (ToR) switch.
- These packets forcefully remap the Virtual IP (VIP) to the new active node's physical MAC address.
- The switch updates its routing tables instantly, redirecting all incoming TCP/UDP traffic to the new node within a single network frame interval.

## 4. Deterministic State Machine Transitions
The orchestrator operates purely on C++ template state transitions (`slabflux::workflow::state_machine`), ensuring that failover logic contains zero dynamic memory allocations or blocking mutexes.
Transitions (e.g., `STANDBY -> ACTIVE`, `ACTIVE -> DEMOTED`) instantly toggle hardware boundaries, such as:
- Un-muting the `baremetal_egress` sockets to allow outbound trading.
- Promoting the local `hlc_clock` to the authoritative cluster timeline.