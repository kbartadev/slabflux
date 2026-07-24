# ADR 004: Explicit Thread-Affinity Strategy

## Status
Accepted

## Context
In ultra-low latency environments, operating-system-level thread migration between CPU cores causes catastrophic cache-line invalidation and pipeline stalls. Relying on the default Linux/Windows scheduler is insufficient for HFT or real-time control requirements.

## Decision
All critical `pipeline` and `conduit` consumers must be pinned to dedicated physical CPU cores. The system must provide a topology configuration mechanism to allow the runtime to enforce core affinity (pinning) at initialization.

## Consequences
- **Positives**: Eliminates latency jitter caused by OS preemption and thread migration. Ensures consistent L1/L2 cache locality.
- **Negatives**: Requires the underlying platform to expose core IDs and restricts the system to specific hardware topologies.
