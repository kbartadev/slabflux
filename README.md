<p align="center">
  <img src="./assets/slabflux.svg" alt="Project Logo">
</p>

---

## Overview

**SlabFlux** is a modern C++20 deterministic execution layer designed for systems where latency, jitter, and contention directly impact correctness or business value.  
It operates entirely in userspace and provides OS‑level control over scheduling, memory topology, and network I/O—without replacing the host operating system.

SlabFlux is not a traditional “framework.”  
It is a **kernel‑bypass, deterministic compute substrate** engineered for:

- high‑frequency trading,
- real‑time telemetry,
- robotics and autonomous systems,
- ultra‑low‑latency distributed services,
- and any workload where microseconds matter.

By eliminating nondeterministic kernel paths and enforcing strict hardware‑sympathetic execution, SlabFlux enables sub‑microsecond end‑to‑end processing pipelines.

---

## Core Principles

### Deterministic Execution
All pipelines, handlers, and state transitions are cycle‑bounded, branch‑predictable, and jitter‑free.

### Zero Contention
Concurrency is built on SPSC/SPSC‑mesh lock‑free conduits.  
No mutexes, no atomics storms, no shared hot cache lines.

### Zero Allocation
After ignition, SlabFlux forbids dynamic allocation.  
Memory is sourced from pre‑faulted HugePage arenas.

### Hardware‑Sympathetic
Direct control over:

- NUMA placement  
- CPU affinity  
- L3 cache partitioning  
- TLB pre‑faulting  
- AVX‑512/AVX2 vector lanes  

### Kernel‑Bypass I/O
Native integration with:

- **AF_XDP**  
- **DPDK**  
- **io_uring**  
- **bare‑metal NIC egress**  
- **shared‑memory IPC**  

---

## Architecture

SlabFlux is composed of tightly integrated subsystems:

### Core (`slabflux::core`)
Deterministic event lifecycle, HugePage allocator, memory topology, hardware barriers.

### Compute (`slabflux::compute`)
AVX‑accelerated kernels, temporal guards, cycle‑budget enforcement.

### Network & Transport (`slabflux::net`, `slabflux::transport`)
Kernel‑bypass ingress/egress, TCP stream fragmenter/defragmenter, AVX‑optimized HTTP/JSON pipelines.

### Concurrency (`slabflux::core::conduits`)
SPSC/SPSC‑mesh lock‑free conduits with deterministic routing and zero‑stall semantics.

### Distributed Systems (`slabflux::dist`)
Causal mesh, state replication, snapshot sync, failover orchestration.

### Telemetry (`slabflux::hw`)
Nanoscope tracing, TSC‑based timing, cache‑line heatmaps.

### Workflow (`slabflux::workflow`)
Compile‑time DAG routing, ephemeral context engine, deterministic state machines.

---

## Why SlabFlux?

Traditional OS kernels introduce:

- nondeterministic scheduling,  
- unpredictable preemption,  
- cache‑unaware memory movement,  
- NUMA‑unaware thread placement,  
- and high‑variance network I/O paths.

SlabFlux provides:

- deterministic scheduling,  
- deterministic memory access,  
- deterministic network I/O,  
- deterministic state transitions,  
- and deterministic distributed behavior.

This makes it ideal for workloads where latency = revenue or latency = safety.

---

## Use Cases

SlabFlux is engineered for:

- High‑Frequency Trading (HFT)  
- Market‑Making Engines  
- Real‑Time Telemetry  
- Robotics & Autonomous Systems  
- Edge Compute / 5G Core  
- AI Inference Pipelines  
- Ultra‑Low‑Latency Game Servers  
- Industrial Automation / MES  
- Distributed Consensus Systems  

---

## Build Instructions

### Linux (Production)
- GCC 12+ / Clang 16+  
- AVX‑512 recommended  
- Requires: `libxdp`, `libbpf`, `liburing`, `libnuma`

### Windows (Research)
- Visual Studio 2022  
- Best‑effort determinism

---

## Documentation

*   **[Documentation Portal](./PORTAL.md)**: Detailed architectural deep-dives, API references, and design rationales.
*   **[Specification](./docs/global.index.md)**: The primary entry point for architectural deep-dives, API references, and design rationales.
*   **[Usage Examples](./examples/INDEX.md)**: Implementation patterns ranging from basic SPSC buses to **Autonomous Drone ECS** and high-performance sharding.
*   **[Benchmarks & Methodology](./benchmarks/INDEX.md)**: Source code and protocols for all measurements, including atomic fences, cache-line isolation, and verification.
*   **[Technical Mastery](./docs/tutorials/tut.index.md)**: Guided walkthroughs for mastering C++20 Concept-based pipelines, O(1) memory management, and Hybrid SSO string handling.
*   **[Technology Map & External References](./docs/technology_map/global.index.md)**: A high-level overview of internal component relationships and dependency hierarchy. Structured bibliography of foundational research in lock-free algorithms and high-performance I/O.
*   **[Decision Records (ADRs)](./docs/adrs/adr.index.md)**: Rationale for rejecting OOP Inheritance (ADR 001), Union-Based Slab Pools (ADR 002), and banning MPSC Queues (ADR 003).


---

## Licensing

SlabFlux is distributed under a **Fair‑Code** license:

- Free for individual developers, researchers, and independent projects
- Restricted for corporate production use.

For formal copyright validation, legal compliance reviews, or intellectual property inquiries, please contact the author directly via the dedicated channel:

**Contact:** [kbartadev@gmail.com](mailto:kbartadev@gmail.com)

*(Please refer to the [LICENSE.md](LICENSE.md), [NOTICE.md](NOTICE.md), and [LEGAL_FAQ.md](LEGAL_FAQ.md) files for complete terms, hardware performance advisories, plain-English usage boundaries, and standard liability disclaimers).*

---

## Summary

SlabFlux is a **deterministic userspace execution layer** that provides:

- OS‑level control without replacing the OS  
- kernel‑bypass I/O  
- zero‑contention concurrency  
- zero‑allocation hot paths  
- hardware‑sympathetic scheduling  
- and sub‑microsecond end‑to‑end latency

It is designed for systems where latency and predictability are essential.
