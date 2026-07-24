# Architecture and Philosophy

The SLABFLUX Runtime Environment (RTE) employs a rigorously layered architecture, engineered to guarantee ultra-low latency (ULL) and deterministic execution by strictly isolating domain logic.

## Directory Structure

* `slabflux/core/`: The lowest-level, OS-agnostic foundational data structures (e.g., Lock-Free Pools, Fenced Events, Timing Wheels, Vector SIMD Engines).
* `slabflux/bridge/`: Deterministic coordination mechanisms (Conduits, Pipelines). Responsible for the frictionless routing of events across thread and core boundaries.
* `slabflux/io/` and `slabflux/net/`: Physical I/O and network interfacing. Houses OS-specific asynchronous bypasses (e.g., Linux `io_uring`) and TCP boundary routers (`network_conduit`).
* `slabflux/transport/`: Ultra-low-latency implementations of application-layer protocols (e.g., the AVX2-accelerated `baremetal_parser` for HTTP).

## The Journey of the Code (Hot Path)

1. **Ingress:** The network interface card (NIC) or `io_uring`/TCP socket pushes incoming data directly into an L2/L3-aligned memory region, completely bypassing kernel interrupts.
2. **Parse:** The `transport` layer processes raw byte streams in guaranteed O(1) time utilizing hardware SIMD instructions (`_mm256_load_si256`).
3. **Dispatch:** The `conduit` and `pipeline` matrices route the event to the designated business logic with zero lock contention.
4. **Process:** Stateful handlers (implemented as plain C++ structs without virtual dispatch or base classes) execute market logic.
5. **Reclaim:** Upon exiting the scope, the `event_ptr` smart pointer deterministically returns memory to the LIFO `pool` in strict O(1) time.
