# 🗺️ Technology Map Index

This directory serves as the master structural index for the **SLABFLUX** subsystem layout, component relationships, and internal dependency constraints. Each architectural module is isolated into its own domain tracking technical specifications, data flows, and API boundary configurations.

---

## 🔲 Subsystem Navigation Matrix
| Subsystem Layer | Architectural Directive | Technology Blueprint | Foundation |
| :--- | :--- | :---: | :---: |
| **Core** | Low-level clock steering, HugePage memory management, and SPSC/MPMC conduits. | [Blueprint](./core/blueprint_core.md) | [Foundation](./core/foundation_core.md) |
| **IO** | Kernel-bypass ingress, io_uring orchestration, and zero-syscall networking. | [Blueprint](./io/blueprint_io.md) | [Foundation](./io/foundation_io.md) |
| **Compute** | AVX2/AVX-512 state processing engines, temporal limits, and cycle-budget enforcers. | [Blueprint](./compute/blueprint_compute.md) | [Foundation](./compute/foundation_compute.md) |
| **Bridge** | NUMA-local state publication seqlocks, cross-context fences, and asynchronous buffers. | [Blueprint](./bridge/blueprint_bridge.md) | [Foundation](./bridge/foundation_bridge.md) |
| **HFT** | Multicast receivers, zero-copy protocol engines, and direct pipeline bypass tracks. | [Blueprint](./hft/blueprint_hft.md) | [Foundation](./hft/foundation_hft.md) |
| **Net** | User-space network sockets, socket orchestration utilities, and packet ring buffers. | [Blueprint](./net/blueprint_net.md) | [Foundation](./net/foundation_net.md) |
| **Transport** | Zero-allocation HTTP/1.1 DFA text tokenizers and high-frequency REST gateways. | [Blueprint](./transport/blueprint_transport.md) | [Foundation](./transport/foundation_transport.md) |
| **Storage** | Sector-aligned, O_DIRECT append-only Write-Ahead Logging write systems. | [Blueprint](./storage/blueprint_storage.md) | [Foundation](./storage/foundation_storage.md) |
| **Platform** | OS abstraction primitives, thread affinity configurations, and hardware mappings. | [Blueprint](./platform/blueprint_os.md) | [Foundation](./platform/foundation_os.md) |
| **Domain** | Specialized execution workloads, context routing payloads, and macro state machines. | [Blueprint](./domain/blueprint_logic.md) | [Foundation](./domain/foundation_logic.md) |
| **Supplemental** | Environmental diagnostics, background telemetry exporters, and active fault simulators. | [Blueprint](./supplemental/blueprint_supplemental.md) | [Foundation](./supplemental/foundation_supplemental.md) |
| **Workflow** | Deterministic business logic, zero‑allocation state transitions, and concept enforcement. | [Blueprint](./workflow/blueprint_logic.md) | [Foundation](./workflow/foundation_logic.md) |
| **HW Layer** | Compiler-specific intrinsics, execution loop unrolling, and FPGA PCIe MMIO extensions. | [Blueprint](./hw/blueprint_intrinsics.md) | [Foundation](./hw/foundation_intrinsics.md) |
| **Logic** | Fixed-size arrays, branchless bounds checking, and deterministic state machines. | [Blueprint](./logic/blueprint_logic.md) | [Foundation](./logic/foundation_logic.md) |
| **Oracle** | Ingress normalization, hardware timestamping, and atomic RDMA multicast sequencing. | [Blueprint](./oracle/blueprint_oracle.md) | [Foundation](./oracle/foundation_oracle.md) |
| **Security** | Zero-latency ingress authenticity and dual-core software lock-step verification checkers. | [Blueprint](./security/blueprint_security.md) | [Foundation](./security/foundation_security.md) |
| **Test** | Measuring resilience via deterministic fault injection and hardware bit-flip simulations. | [Blueprint](./test/blueprint_test.md) | [Foundation](./test/foundation_test.md) |
| **Runtime** | Hardware core isolation, thread pinning affinity, and wait-free tight polling loops. | [Blueprint](./runtime/blueprint_runtime.md) | [Foundation](./runtime/foundation_runtime.md) |
| **Orchestration** | Multi-node state manifolds, SIMD matrix propagation, and O(1) route detachment. | [Blueprint](./orchestration/blueprint_orchestration.md) | [Foundation](./orchestration/foundation_orchestration.md) |
| **Mesh** | Distributed cluster vector clocks and zero-allocation out-of-order parking lots. | [Blueprint](./mesh/blueprint_dist.md) | [Foundation](./mesh/foundation_dist.md) |
| **Debug** | Deep-state offline validation pipelines and hardware-accelerated state hashing. | [Blueprint](./debug/blueprint_audit.md) | [Foundation](./debug/foundation_audit.md) |
| **Sys** | Uncore ratio locking, Intel CAT sets partitioning, and raw MCE diagnostic monitors. | [Blueprint](./sys/blueprint_sys.md) | [Foundation](./sys/foundation_sys.md) |
| **AI** | Neural token processing, AVX-512 GEMV kernels, and weight cryptographic attestation. | [Blueprint](./ai/blueprint_ai.md) | [Foundation](./ai/foundation_ai.md) |
| **Management** | Structural honesty telemetry monitors, lock-free error buffers, and hardware fencing. | [Blueprint](./mgmt/blueprint_telemetry.md) | [Foundation](./mgmt/foundation_telemetry.md) |

---

## 🏗️ Detailed Module Breakdown

### 1. Core Platform Layer (`slabflux::core`)
The foundation of the runtime loop execution engine. Handles lock-free memory layout reclamation, thread scheduling bypass mechanics, and high-frequency communication pipelines.
* **Core Subsystem Index**: Central entry point for core primitives, memory management, and physical topologies.

### 2. Computational Kernels (`slabflux::compute`)
Hardware-accelerated mathematics and runtime lifecycle checking logic. Ensures computational safety without introducing branch evaluation latency.
* **[Compute Technology Map](./compute/TECHNOLOGY_MAP.md)**: AVX-512/AVX2 registers mapping tracks and execution layouts.
* **[Compute API Reference](./compute/REFERENCE.md)**: Boundary constraints covering `vector_lane_engine`, `temporal_guard`, and `error_arbiter`.

### 3. Synchronization Bridges (`slabflux::bridge`)
Inter-thread and cross-core communication matrices separating execution planes while preventing cache line invalidation.
* **[Bridge Technology Map](./bridge/TECHNOLOGY_MAP.md)**: NUMA boundary topologies and memory boundary fence allocations.
* **[Bridge API Reference](./bridge/REFERENCE.md)**: Layout parameters for `spsc_data_bridge` and `mpmc_event_bridge`.

### 4. High-Frequency Trading Synapses (`slabflux::hft`)
Specialized ingestion and execution components designed for direct sub-microsecond line-rate evaluation.
* **[HFT Technology Map](./hft/TECHNOLOGY_MAP.md)**: Direct kernel-bypass memory mapping lines and validation pathways.
* **[HFT API Reference](./hft/REFERENCE.md)**: Interface bindings for `zero_copy_egress` and lock-free multicast connectors.

### 5. Network Topology Layer (`slabflux::net`)
Bypasses typical socket orchestration structures to drive raw inbound and outbound data arrays.
* **[Net Technology Map](./net/TECHNOLOGY_MAP.md)**: Network loop tracking matrices and packet ring topologies.
* **[Net API Reference](./net/REFERENCE.md)**: Method specifications for `network_conduit` and sequence replication instances.

### 6. Transport Protocol Translators (`slabflux::transport`)
Fast text interpretation systems converting raw wire bytes into structured runtime execution contexts.
* **[Transport Technology Map](./transport/TECHNOLOGY_MAP.md)**: State transition tables for our custom branchless string parsing loops.
* **[Transport API Reference](./transport/REFERENCE.md)**: API configurations for `baremetal_parser` and structured frame layouts.

### 7. High-Performance Persistence (`slabflux::storage`)
Direct interaction layers handling non-volatile hardware write locations without thread stalling penalties.
* **[Storage Technology Map](./storage/TECHNOLOGY_MAP.md)**: Alignment models linking transaction blocks to raw NVMe physical sector configurations.
* **[Storage API Reference](./storage/REFERENCE.md)**: Lower-level parameters driving `durable_journal` operations.

### 8. Hardening Environment (`slabflux::platform`)
Abstracts and manages operating system configurations, thread pinning controls, and low-level resource masking definitions.
* **[Platform Technology Map](./platform/TECHNOLOGY_MAP.md)**: Hardware layer affinity maps and memory lock tracking frameworks.
* **[Platform API Reference](./platform/REFERENCE.md)**: Host runtime control loops and environment state configuration hooks.

### 9. Execution Workloads (`slabflux::domain`)
User-space application contexts, message parsing definitions, and high-level behavioral matrices.
* **[Domain Technology Map](./domain/TECHNOLOGY_MAP.md)**: High-level system graphs and cross-functional sequence routing paths.
* **[Domain API Reference](./domain/REFERENCE.md)**: Custom payload contracts and system work unit parameters.

### 10. Operational Metrics (`slabflux::supplemental`)
Diagnostic instrumentation, environment variables validation, and background logging managers.
* **[Supplemental Technology Map](./supplemental/TECHNOLOGY_MAP.md)**: Metric aggregation vectors and asynchronous monitoring pipelines.
* **[Supplemental API Reference](./supplemental/REFERENCE.md)**: Non-hot-path diagnostic metrics and active fault simulation hooks.

### 11. I/O Subsystem (`slabflux::io`)
Bare-metal ingress/egress orchestration layer utilizing kernel-bypass techniques.
* **IO Subsystem Index**: Specifications for `io_uring` integration, zero-copy networking, and SQPOLL configurations.
