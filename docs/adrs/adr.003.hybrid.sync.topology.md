# ADR 003: Hybrid Synchronization Topology

## Status
Accepted

## Context
Standard thread-pool architectures use Multi-Producer Single-Consumer (MPSC) or Multi-Producer Multi-Consumer (MPMC) queues for work distribution. The initial architecture banned these to avoid cache contention. However, for non-critical control-plane logic, the overhead of strict SPSC topologies is often disproportionate to the gain.

## Decision
MPSC/MPMC queues are now permitted for control-plane logic (e.g., configuration updates, telemetry aggregation, system signals). The data-plane (hot-path) remains strictly restricted to SPSC `conduit` channels to maintain O(1) determinism.

## Consequences
- **Positives**: Increased flexibility for stateful management and configuration synchronization without blocking the hot-path.
- **Negatives**: Requires explicit segregation; developers must ensure that high-throughput packet processing is never routed through non-SPSC channels.
