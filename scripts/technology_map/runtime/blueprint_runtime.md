# Blueprint: Runtime Architecture

## Architectural Overview
The SlabFlux Runtime subsystem acts as the overarching sovereign execution domain, seizing absolute control over processor cores, orchestrating memory layouts, and initializing the infinite lock-free polling loops while strictly excluding operating system scheduler interference.

## Core Components
- **Domain Orchestrator**: Bootstraps the execution environment by enforcing NUMA-aware physical memory bounds and deploying strictly aligned `spsc_conduit` structures between designated worker threads.
- **Bimodal Execution Bootstrapper**: Physically separates deterministic O(1) logic (Hot Path) from stateful, non-deterministic tasks (Cold Path), assigning them to independent CPU domains upon engine ignition.
- **Graceful Drain & Poison Pill**: Implements deterministic shutdown procedures utilizing lock-free event tokens (Poison Pills) instead of immediate POSIX signal termination, guaranteeing no partially corrupted states or memory leaks upon application exit.
- **Lifecycle Matrix**: Manages the ephemeral injection of stack-allocated execution contexts across the cascading dispatch pipeline on a per-tick basis.