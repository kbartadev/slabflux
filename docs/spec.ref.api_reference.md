# SLABFLUX - API Reference

## Architectural Invariants (Critical Path Checklist)
To maintain deterministic `O(1)` performance and system stability, all developers must adhere to these constraints within the hot-path:

* [ ] **Ownership Validation**: Every `make_raw()` call must have a corresponding, deterministic `release()` call. Manual lifecycle management requires rigorous tracking; leaks will silently saturate pool capacity.
* [ ] **Syscall Prohibition**: No system calls (e.g., `log`, `printf`, `mutex`, `file I/O`) are permitted within the hot-path. Use `slabflux::supplemental::async_logger` for I/O.
* [ ] **Thread Affinity**: Threads must be pinned to dedicated CPU cores using `pin_thread` before using `local_pool` or `spsc_ring_conduit` primitives.
* [ ] **False Sharing Prevention**: All hot-path data structures must be decorated with `alignas(64)` to prevent cache-line contention between concurrent threads.
* [ ] **Wait-Free Preference**: Always prefer `spsc_ring_conduit` or `spsc_pool` for Gateway-to-Compute handoffs to avoid the `CAS` overhead inherent in `mpmc` variants.
* [ ] **Memory Alignment**: Ensure all data payload sizes are multiples of 64 bytes to prevent cache-line splitting.
* [ ] **Branch Minimization**: Use `SL_EXPECT_TRUE/FALSE` markers for paths with >99% predictability.

---

## Core Infrastructure (`slabflux::`), (`slabflux::core::`)
The foundational layer providing deterministic `O(1)` memory and event ownership.

| Component | Description |
| :--- | :--- |
| `slabflux::core::runtime_domain<Events...>` | Logical container managing typed pools and providing `O(1)` allocation across multiple event types. |
| `slabflux::core::timing_wheel` | `O(1)` hashed timing wheel for high-resolution timer events. |
| `slabflux::core::hardware_topology` | NUMA-aware physical memory mapping and CPU topology constraints. |
| `slabflux::core::physical_layout` | Struct layout compiler definitions enforcing L1/L2 spatial locality. |
| `slabflux::core::pinned_allocator_spsc` | Explicit-lifecycle allocator avoiding OS scheduler intervention. (SPSC) |
| `slabflux::core::pinned_allocator_mpmc` | Explicit-lifecycle allocator avoiding OS scheduler intervention. (MPMC) |

---

### SLABFLUX Pool Architecture (`slabflux::core::`)

| Pool Type | Concurrency Model | Synchronization Cost | Recommended Use Case |
| :--- | :--- | :--- | :--- |
| `local_pool<T, N>` | **Thread-Local** | **Near-Zero** | TLS-based. Ideal for NUMA-local tasks pinned to a single socket. |
| `spsc_pool<T, N>` | **Wait-Free** | **Minimal** | Inter-thread handoff (Gateway → Compute). Optimized for SPSC rings. |
| `mpsc_pool<T, N>` | **Lock-Free** | **Moderate** | Multi-producer (e.g., NIC queues) feeding a single worker thread. |
| `mpmc_pool<T, N>` | **Lock-Free** | **Highest** | High-contention, many-to-many. Uses sequence-tagged CAS for ABA protection. |
| `pool<T, N>` | **Lock-Free (Alias)** | **Highest** | **Alias for `mpmc_pool`**. General purpose, but carries CAS overhead. |

---

### SLABFLUX Conduit Architecture (`slabflux::core::`)

| Conduit Type | Concurrency Model | Synchronization Cost | Recommended Use Case |
| :--- | :--- | :--- | :--- |
| `spsc_ring_conduit<T*, N>` | **Wait-Free** | **Minimal** | Core HFT API. Raw buffer access for zero-latency ingress/egress. |
| `spsc_conduit<T*, N>` | **Wait-Free** | **Minimal** | Hardware-ring buffer using C++20 `<bit>` alignment. |
| `mpmc_conduit<T*, N>` | **Lock-Free** | **Moderate** | Multi-producer event bus. High-contention routing using sequence-tagging. |
| `conduit<T*, N>` | **Lock-Free** | **Moderate** | **Alias for `mpmc_conduit`**. Default architectural interface for multi-threaded dataflow. |

---

## Routing & Logic Flow (`slabflux::core::`)
Micro-latency communication and compile-time execution pipelines.

| Component | Description |
| :--- | :--- |
| `slabflux::pipeline<Handlers...>` | Compile-time event chaining (Matrix Fusion) that eliminates virtual function overhead. |
| `slabflux::bound_sink<Pipeline, Event>` | Connects a specific pipeline endpoint to a dedicated event type. |
| `slabflux::round_robin_switch` | `O(1)` wait-free event sharder (Fan-out) for deterministic load balancing. |
| `slabflux::round_robin_poller` | Fair event extractor (Fan-in) for gathering data from multiple sources (e.g., primary/backup feeds). |

---

## Deterministic Compute (`slabflux::compute::`)
Hardware-accelerated computational kernels and execution guards.

| Component | Description |
| :--- | :--- |
| `error_arbiter` | Resiliency logic that quarantines bad events into a lock-free ring instead of crashing. |
| `fault_severity` | Enum for indicating fault severity (WARNING, CRITICAL, PANIC). |
| `fault_record` | 16-byte atomic record containing error codes and LSN markers for auditing/error_arbiter. |
| `avx512_search_backend` | AVX-512 optimized state container capable of 8-key searches per cycle. |
| `branchless_engine` | Execution motor featuring zero-branch logic and integrated temporal guards. |
| `temporal_guard` | In-band cycle-budget enforcer that detects processing stalls via RDTSC. |
| `intrinsics` | Platform-agnostic wrappers for SIMD (AVX2/AVX-512), BMI, and cache control. |
| `path_guard` | Compile-time directives forcing unrolled branches into linear execution limits. |
| `vector_lane_512` | 512-bit wide explicit hardware data lane mapping. |
| `vector_lane_engine` | Data parallel processor mapping sequential algorithms to lane architectures. |
| `hw::intrinsics` | Deep silicon mapping for bit-twiddling (`tzcnt`, `bsf`, `clwb`). |

---

## Bridge & State Synchronization (`slabflux::bridge::`)
Asynchronous bridges between non-deterministic layers and the pinned RTE cores.

| Component | Description |
| :--- | :--- |
| `bridge_sync` | NUMA-local state publisher using Seqlock for consistent world-snapshots. |
| `authoritative_bridge` | Non-blocking "truth" serialization that prevents I/O stalls during NVMe journaling. |

---

## Precision Signal Routing (`slabflux::conduit::`)
Technical signal wrappers and hardware multiplexing.

| Component | Description |
| :--- | :--- |
| `iron_ring_buffer` | Enhanced SIMD buffer featuring software pre-fetching to hide cache latency. |
| `signal` | Envelope providing RDTSC hardware tagging and CRC32 data integrity. |
| `simd_vector` | Hardware-aligned (alignas 32/64 byte) vector signal structure. |
| `slabflux::net::multicast_egress` | UDP Multicast broadcaster node for low-latency state replication. |
| `slabflux::net::delta_broadcaster` | Dispatches sparse bit-diff changes instead of full frames. |
| `slabflux::net::delta_compressor` | Hardware-accelerated bit-packing mechanism for delta replication. |
| `slabflux::net::retransmission_buffer` | O(1) sliding window holding recent frames for loss-recovery logic. |
| `slabflux::net::nack_handler` | Engine to parse Negative Acknowledgements and fire retransmissions. |
| `slabflux::net::mesh_router` | Layer 3/4 internal mesh packet switcher. |
| `slabflux::net::bridge` | Secure gateway validating external ingress events against deterministic rules. |
| `slabflux::net::backpressure_monitor` | TCP window scale tracking to throttle egress during network spikes. |

---

## Time, Cluster & Topology
Precision synchronization and distributed system coordination.

| Component | Description |
| :--- | :--- |
| `time` & `clock_node` | The absolute, drift-corrected internal time source. |
| `hlc_clock` | Hybrid Logical Clock implementation for causal event ordering across the mesh. |
| `ptp_clock_mapper` | Hardware NIC timestamp-to-system-time mapper. |
| `clock_steerer` | Logic for continuous drift correction of the timeline. |
| `lsn_heartbeat` | Microsecond-precision stall detector for sequence numbers. |
| `causal_mesh` | Topological mapping defining connected instances. |
| `causal_header` | Bit-exact header for network events ensuring global causal order. |
| `mesh_map` | Wait-free registry for process discovery and IPC shared memory mapping. |
| `static_topology` | Compile-time hardware specification for ultra-optimized deployments. |
| `failover_orchestrator` | High-availability state manager for the cluster. |
| `fused_nexus_node` | Authoritative state anchor preventing split-brain scenarios. |
| `wire_frame_lsn` | Unit comprising a network frame payload and its associated LSN sequence number. |
| `state_replay_ring` | Specialized cluster-state container for global synchronization. |
| `cluster_orchestrator` | Central state machine orchestrating network rebinding and node life-cycles. |
| `slabflux::dist::distributed_mesh_router` | IPC/Network routing node ensuring causally consistent delivery. |
| `slabflux::dist::discovery_beacon` | Multicast presence announcer for auto-forming the cluster nodes. |
| `slabflux::dist::rdma_fabric` | Remote Direct Memory Access wrapper for sub-microsecond memory sync. |
| `slabflux::dist::causal_sequencer` | Applies strict happens-before logical ordering to cross-node events. |
| `slabflux::dist::durable_saga_orchestrator` | Distributed rollback-enabled two-phase commit manager. |
| `slabflux::dist::failover_signal` | Hardware-priority UDP heartbeat ensuring active/passive failovers. |
| `slabflux::dist::gateway_node` | Border ingress proxy securing internal node traffic. |
| `slabflux::dist::node_directory` | Static array mapping logical nodes to physical IPs. |
| `slabflux::orchestration::distributed_state_matrix` | Read-only matrix aggregating memory views from multiple machines. |
| `slabflux::sys::topology_enforcer` | Validation logic enforcing `hwloc` CPU affinities and NUMA restrictions. |
| `slabflux::sys::topology_scanner` | Initialization-time PCI/NUMA interrogator aligning system resources. |

---

## String Services (`slabflux::core::`)
Zero-allocation and chunk-based string management.

| Component | Description |
| :--- | :--- |
| `fixed_string<N>` | Stack-based, trivially copyable string container. |
| `smart_string` | Dynamic, zero-allocation string wrapper utilizing fragmented chunks. |
| `string_service` | Lifecycle manager for `smart_string` chunk pools. |
| `string_chunk` | Backend node structure representing a strictly-sized fragment of a `smart_string`. |
| `global_string_pool` | System-wide, thread-local aware lock-free allocator for string chunks. |

---

## Transport, I/O & Persistence
Linux-native zero-syscall I/O and protocol parsing.

| Component | Description |
| :--- | :--- |
| `io_uring_ingress` | High-performance Linux-native zero-syscall I/O loop. |
| `async_journal` | Asynchronous write-ahead logging (WAL) for system state. |
| `network_conduit` | TCP-based conduit operating via `bind_socket` and `poll_tx/rx`. |
| `matrix_nexus` | Protocol-agnostic entry point binding network data directly to the logical core. |
| `http_request_event` | 64-byte aligned HTTP event with AVX2 buffer. |
| `http_parser` | `O(1)` AVX2-accelerated request parser. |
| `slabflux::io::af_xdp_ingress` | Linux AF_XDP socket wrapper for kernel-bypass packet reception. |
| `slabflux::io::baremetal_egress` | Raw socket/NIC driver interface for minimal latency transmission logic. |
| `slabflux::io::eader_parser` | Rapid protocol header dissection (IPv4/UDP) inside the ingress pipeline. |
| `slabflux::io::hardware_shaper` | Configures NIC traffic control (TC) to prevent outgoing micro-bursts. |
| `slabflux::io::header_parser` | High-speed SIMD token scanner for plain-text protocol dissection. |
| `slabflux::io::mirrored_journal` | Dual-NVMe logic ensuring fault-tolerance across mirrored RAID blocks. |
| `slabflux::io::egress` | Single-threaded egress loop monopolizing a dedicated core for TCP flushes. |
| `slabflux::io::stack` | Bare-metal TCP/IP stack implementation bypassing the OS completely. |
| `slabflux::io::wire_latency_monitor` | Analyzer comparing NIC hardware PTP timestamps against software arrival. |
| `slabflux::storage::durable_journal` | Backing interface for zero-copy, append-only WAL to NVMe block devices. |
| `slabflux::storage::durable_sink` | O_DIRECT logical consumer for persistence data. |
| `slabflux::storage::durable_source` | High-speed file reader loading historical data for Replay. |
| `slabflux::storage::durable_storage` | Manager organizing raw NVMe block layouts. |
| `slabflux::hft::zero_copy_egress` | Direct DMA memory mapping construct for outgoing market orders. |
| `slabflux::hft::stall_free_nexus` | Busy-polling network ingress loop designed to completely monopolize the CPU. |
| `slabflux::hft::matrix_nexus` | Multi-cast receiver dropping packets directly into thread-local pools without locks. |
| `slabflux::net::server_ingress` | Abstract handler orchestrating client TCP socket acceptance. |
| `slabflux::net::network_replicator` | Node pushing synchronized states out to connected passive readers. |
| `slabflux::net::nexus_connector` | Ties hardware Ingress events to the internal logical mesh network. |
| `slabflux::net::snapshot_sync` | Point-to-point mass memory transfer logic for booting new nodes. |
| `slabflux::net::uring_ingress_stream` | `io_uring` tuned specifically for SQPOLL and fixed buffers in the net stack. |
| `slabflux::transport::http` | Main HTTP definitions and structural schemas. |
| `slabflux::transport::http_gateway` | Bridges bare-metal socket traffic into deterministic HTTP pipelines. |
| `slabflux::transport::http_avx` | Specialized SIMD pipeline exclusively for microsecond REST APIs. |

---

## Hardware Telemetry & Silicon Guards (`slabflux::core::`)
Low-level monitoring and system-wide isolation mechanisms.

| Component | Description |
| :--- | :--- |
| `chip_telemetry` | Collector for hardware-level arrival and processing statistics. |
| `stack_guard` | Pre-faulting mechanism for stack memory to avoid runtime page faults. |
| `fpu_shield` | Logic for "engaging" and warming up FPU/SIMD registers. |
| `backpressure_valve` | Deterministic flow regulator monitoring the LSN horizon. |
| `integrity_seal` | Hardware-level memory encryption and CRC32 validation interface. |
| `integrity_guard` | Magic-number boundary definitions generating faults on memory overruns. |
| `integrity_validator` | Verifier blocking pipeline continuation if memory corruptions are detected. |
| `sys::hardware_telemetry` | Direct reader for Performance Monitoring Counters (PMC). |
| `sys::lbr_analyzer` | Zero-overhead hardware tracer utilizing Last Branch Records. |
| `sys::smi_monitor` | OS-invisible latency detector for System Management Interrupts. |
| `sys::msr_silencer` | Model Specific Register tuner for CPU frequency/power control. |
| `sys::ecc_monitor` | Tracker for memory degradation and ECC corrections. |
| `sys::mce_listener` | Emergency handler for Machine Check Exceptions. |
| `sys::cache_partitioner` | API for Intel CAT (Cache Allocation Technology). |
| `sys::power_governor` | Controller for CPU C-states and P-states. |
| `sys::isa_guard` | CPUID-based instruction set validator. |
| `sys::layout_guard` | Compile-time cache-line alignment and memory layout verifier. |
| `sys::pcie_aer_guard` | Monitor for PCIe Advanced Error Reporting. |
| `sys::signal_shield` | POSIX signal masker and deterministic translator. |
| `sys::slab_scrubber` | Background deterministic memory zeroing service. |
| `rte::ignition_manifest` | Physical environment validation protocol (cores, memory, priorities). |
| `core::buffer_flush` | Cache-line optimized forced memory flushing utilities (`CLFLUSHOPT`/`CLWB`). |
| `core::cache_shield` | L1/L2 cache partitioning boundary definition to prevent eviction. |
| `sys::thermal_guard` | Hardware throttling detectors triggering alarms on extreme die temperatures. |
| `sys::thermal_soak` | Initial CPU spinner forcing frequencies to maximum before execution. |
| `sys::watchdog_shield` | OS watchdog (`/dev/watchdog`) feeder running pinned on isolated utility cores. |
| `core::board_monitor` | Cross-node telemetry aggregator evaluating cluster-wide hardware health. |
| `core::deterministic_policer` | Hardware-aligned token bucket rate limiter dropping excess packets. |
| `core::hole_puncher` | O(1) sequence gap analyzer waiting for missing logic frames. |
| `core::hot_path_alignment` | Macro directives aligning instructions explicitly to physical I-Cache boundaries. |
| `core::instruction_shield` | Defines explicit sections to prevent kernel swapping of logic blocks. |
| `core::liveness_watchdog` | Internal timeline monitor generating PANICs on stalled processors. |
| `core::speculative_guard` | Execution fencing mechanisms mitigating speculative branching exploits. |
| `core::symbol_guard` | Verification ensuring forbidden shared libraries (e.g. glibc) are not linked. |
| `sys::alignment_checks` | C++ constraints terminating builds if padding assumptions break. |
| `sys::audit_ledger` | Append-only non-blocking diagnostic memory region for internal fault tracking. |
| `sys::binary_seal` | Locks executables into RAM (`mlockall`) and verifies signatures. |
| `sys::blackbox_recorder` | Circular flight-data memory writing hardware exceptions to disk. |
| `sys::entropy_anchor` | CPU hardware seed collector (`RDRAND`) feeding logic generators. |
| `sys::heartbeat_monitor` | Thread stall verifier confirming pipeline continuation. |
| `sys::pps_latch` | NIC/Hardware pulse-per-second syncer aligning machine time. |
| `sys::entropy` | Highly robust, cryptographically sound pool avoiding OS entropy starving. |
| `sys::state_migrator` | Moves logic blocks physically across NUMA boundaries in response to latency. |
| `sys::telemetry_node` | SPSC-driven background compiler of statistical hardware events. |
| `sys::tick_event` | The fundamental structural engine beat that drives all logic blocks. |
| `sys::tier_guard` | Compile-time verifier ensuring architecture layers don't access banned data. |
| `sys::tpm_attestor` | Hardware interface generating cryptographic proofs of valid software boot. |
| `sys::uncore_lock` | Forces Intel Uncore/Ring-bus frequencies to absolute maximums. |
| `rte::jitter_audit` | Calculates system noise post-ignition to ensure stable C-States. |

---

## Main Execution & Control

| Component | Description |
| :--- | :--- |
| `slabflux::core::core` | Master orchestrator for boot sequence, thread pinning, and resource claiming. |
| `slabflux::core::immutable_config` & `config_bridge` | Sealed, O(1) configuration registry. |
| `slabflux::core::schema` | Static typing for configuration and cluster state layout. |
| `slabflux::core::stack` | Deterministic, pre-faulted custom call stack environment for handlers. |
| `slabflux::core::sf_node_ctx` | Context tracking Logical Sequence Numbers and commit points per thread. |
| `slabflux::core::static_config` | Compile-time environment configuration parser. |
| `slabflux::core::hotpatch_bridge` | Instruction-level code swapper for zero-downtime hot logic replacement. |
| `slabflux::workflow::state_machine` | Zero-allocation, transition-table based deterministic state machine framework. |
| `slabflux::workflow::saga` | Engine coordinating deterministic, multi-stage state compensations. |
| `slabflux::logic::logic` | Core handler integrating domain behavior models. |
| `slabflux::oracle::oracle` | Logic unit resolving external facts deterministically into the mesh. |
| `slabflux::platform::os` | Core wrapper standardizing platform features across execution nodes. |
| `slabflux::platform::fabric` | Cross-process execution environment integration setup. |
| `slabflux::platform::speculative_consensus` | Protocol confirming cluster logic agreements ahead of physical ticks. |
| `slabflux::rte::environment` | Complete standalone namespace wrapper executing the event loop. |
| `slabflux::rte::fractal_node` | Concept object allowing whole environments to run as nested handlers. |
| `slabflux::rte::exit` | Methodical teardown process flushing logs and releasing memory locks. |
| `slabflux::runtime::node_runtime` | Process encapsulator managing thread lifetimes and topology binds. |
| `slabflux::security::secure_ingress` | Payload screening against DDOS boundaries before queue placement. |
| `slabflux::security::judge` | Rule engine banning external IPs at the hardware kernel-bypass level. |
| `slabflux::supplemental::async_logger` | Zero-lock logging pushing char blocks to disk threads. |
| `slabflux::supplemental::blaster_node` | Synthetic load injector executing peak throughput tests. |
| `slabflux::supplemental::chaos_engine` | Framework intentionally breaking state to test orchestration failovers. |
| `slabflux::test::chaos_injector` | In-loop corruptor utilized strictly during QA verification. |
| `slabflux::supplemental::environment` | Helper encapsulator launching background non-hot-path components. |
| `slabflux::supplemental::load_generator` | Fills memory arrays with arbitrary data to force L3 cache stress. |
| `slabflux::supplemental::prometheus_exporter` | Cold-path HTTP server scraping internal counters for metrics dashboards. |
| `slabflux::supplemental::telemetry` | Wait-free aggregator accumulating counts with explicit false-sharing padding. |
| `slabflux::supplemental::telemetry_gateway` | Endpoint bridging raw node stats to unified graphing endpoints. |

## Technical Macros & Annotations

* `SLAB_FORCE_INLINE`: Forced inlining for instruction stream integrity.
* `SLAB_HOT` / `SLAB_COLD`: Path annotations for compiler-level physical code layout.
* `SLAB_LEAF`: Indicates a terminal node function that cannot call back into the RTE core.
* `SLAB_PURE`: Annotation for side-effect-free deterministic computation.
* `SLAB_FLATTEN`: Transforms an entire call tree into a single linear instruction block.
* `SLAB_FLAT_PATH`: Forces loop unrolling and suppresses vectorization for jitter-free execution.
* `SL_EXPECT_TRUE` / `SL_EXPECT_FALSE`: Hardware branch prediction hints.

## OS Isolation Scripts (`/scripts/`)

Before launching the C++ runtime environment, these scripts enforce hardware‑level isolation.

* `bandwidth_enforcer.sh`: Memory bandwidth limiter (Intel MBA) and bandwidth monitor.
* `bus_lock_guard.sh`: Hardware bus‑lock protection and atomic‑operation supervision.
* `cache_partitioner.sh`: Intel CAT (Cache Allocation Technology) configurator and L3 cache locker.
* `interrupt_lock.sh`: Physical hardware interrupt pinning and lockdown.
* `jitter_shield.sh`: Comprehensive operating‑system noise (OS jitter) suppression suite.
* `nic_flow_director.sh`: Network interface card (NIC) flow‑director routing to dedicated cores.
* `pcie_latency_shield.sh`: PCIe ASPM disabler and bus bandwidth tuner (MPS/MRRS).
* `rcu_isolation.sh`: IRQ and RCU callback migration to housekeeping cores.
* `shield_cores.sh`: CPU isolation, cgroup and cpuset partitioner.
* `silicon_priority.sh`: Uncore and ring‑bus frequency pinning, C‑state disabler.
* `init.sh`: Master initialization bootstrapper and environment validator.
* `tdx_seal.sh`: Intel TDX hardware memory‑encryption activator.
* `vfio_shield.sh`: I/O virtualization (SR‑IOV/VFIO) isolation and resource protection.


# SLABFLUX - Audited Roadmap (Code-Missing Components)

## Core Infrastructure (`slabflux::`)
The foundational layer providing deterministic `O(1)` memory and event ownership.

| Component | Description |
| :--- | :--- |
| `slabflux::runtime_domain<Events...>` | Logical container managing typed pools and providing `O(1)` allocation across multiple event types. |
| `slabflux::trivial_serializer<Event>` | POD-only serializer for bit-perfect event encoding and decoding. |
| `slabflux::entity_slab` | Type-aware, contiguous memory slab for domain entities. |
| `slabflux::eternal_memory` | Pre-allocated, never-freed memory pool for static global state. |
| `slabflux::hardware_barrier` | Cross-platform abstractions for memory fences (`lfence`, `sfence`, `mfence`). |
| `slabflux::timing_wheel` | `O(1)` hashed timing wheel for high-resolution timer events. |
| `slabflux::core::hardware_topology` | NUMA-aware physical memory mapping and CPU topology constraints. |
| `slabflux::core::hugepage_allocator` | Zero-copy physical memory allocator targeting 2MB/1GB HugePages. |
| `slabflux::core::huge_slab_pool` | O(1) TLB-miss-free memory pool utilizing HugePages. |
| `slabflux::core::memory_sanitizer` | Hardware-accelerated memory zeroing and isolation routines. |
| `slabflux::core::offset_ptr` | Relative pointer structure designed for zero-copy IPC Shared Memory. |
| `slabflux::core::physical_layout` | Struct layout compiler definitions enforcing L1/L2 spatial locality. |
| `slabflux::core::pinned_slab_allocator` | Explicit-lifecycle allocator avoiding OS scheduler intervention. |
| `slabflux::core::rank_aware_allocator` | Allocator mitigating hardware memory bank conflicts by distributing across DIMM ranks. |
| `slabflux::core::slab_allocator` | Traditional wait-free contiguous block manager. |
| `slabflux::core::tlb_warmup` | Startup routine running memory sweeps to pre-populate the Translation Lookaside Buffer. |

---

### SLABFLUX Pool Architecture Reference

| Pool Type | Concurrency Model | Synchronization Cost | Recommended Use Case |
| :--- | :--- | :--- | :--- |
| `local_pool<T, N>` | **Thread-Local** | **Near-Zero** | TLS-based. Ideal for NUMA-local tasks pinned to a single socket. |
| `spsc_pool<T, N>` | **Wait-Free** | **Minimal** | Inter-thread handoff (Gateway → Compute). Optimized for SPSC rings. |
| `mpsc_pool<T, N>` | **Lock-Free** | **Moderate** | Multi-producer (e.g., NIC queues) feeding a single worker thread. |
| `mpmc_pool<T, N>` | **Lock-Free** | **Highest** | High-contention, many-to-many. Uses C++20 `std::atomic::wait` and epoch-based reclamation. |
| `pool<T, N>` | **Lock-Free (Alias)** | **Highest** | **Alias for `mpmc_pool`**. General purpose, but carries CAS overhead. |

---

### SLABFLUX Conduit Architecture Reference

| Conduit Type | Concurrency Model | Synchronization Cost | Recommended Use Case |
| :--- | :--- | :--- | :--- |
| `spsc_ring_conduit<T*, N>` | **Wait-Free** | **Minimal** | Core HFT API. Raw buffer access for zero-latency ingress/egress. |
| `spsc_conduit<T*, N>` | **Wait-Free** | **Minimal** | Hardware-ring buffer using C++20 `<bit>` alignment. |
| `mpmc_conduit<T*, N>` | **Lock-Free** | **Moderate** | Multi-producer event bus. High-contention routing using sequence-tagging. |
| `conduit<T*, N>` | **Lock-Free** | **Moderate** | **Alias for `mpmc_conduit`**. Default architectural interface for multi-threaded dataflow. |

---

## Routing & Logic Flow (`slabflux::`)
Micro-latency communication and compile-time execution pipelines.

| Component | Description |
| :--- | :--- |
| `slabflux::pipeline<Handlers...>` | Compile-time event chaining (Matrix Fusion) that eliminates virtual function overhead. |
| `slabflux::handler_base<T>` | CRTP base class for developing custom deterministic event handlers. |
| `slabflux::bound_sink<Pipeline, Event>` | Connects a specific pipeline endpoint to a dedicated event type. |
| `slabflux::round_robin_switch` | `O(1)` wait-free event sharder (Fan-out) for deterministic load balancing. |
| `slabflux::round_robin_poller` | Fair event extractor (Fan-in) for gathering data from multiple sources (e.g., primary/backup feeds). |
| `slabflux::conduit` | Fundamental Single-Producer Single-Consumer lock-free ring infrastructure. |. |
| `slabflux::rte::event_arbiter` | Thread-local event scheduling node prioritizing critical paths. |
| `slabflux::rte::flow_controller` | High-watermark congestion manager using 80/40 hysteresis logic. |
| `slabflux::rte::pipeline_warmer` | Emits dummy events to keep Branch Target Buffers (BTB) trained. |
| `slabflux::rte::branch_predictor_warmer` | Specialized loops ensuring branch predictors favor the hot path upon execution. |

---

## Deterministic Compute (`slabflux::compute::`)
Hardware-accelerated computational kernels and execution guards.

| Component | Description |
| :--- | :--- |
| `physics_reactor` | Parallel SIMD mathematical node for environment simulations. |
| `stimulus_event` | Specialized input event for AI/Physics handlers with intensity and confidence metrics. |
| `deterministic_rng` | PCG-based vectorized random number generator seeded strictly by LSN. |
| `replay_manager` | Orchestrator for bit-identical system state reconstruction and silicon "priming". |
| `error_arbiter` | Resiliency logic that quarantines bad events into a lock-free ring instead of crashing. |
| `fault_severity` | Enum for indicating fault severity (WARNING, CRITICAL, PANIC). |
| `fault_record` | 16-byte atomic record containing error codes and LSN markers for auditing/error_arbiter. |
| `avx512_search_backend` | AVX-512 optimized state container capable of 8-key searches per cycle. |
| `branchless_engine` | Execution motor featuring zero-branch logic and integrated temporal guards. |
| `snapshot_manager` | Non-blocking state capture system using `io_uring` and double-buffering. |
| `simd_ring_buffer` | Vectorized sliding-window compute engine. |
| `replay_validator` | Hardware-accelerated CRC32 state hasher for verifying replay integrity. |
| `stack_monitor` | Static analyzer for enforcing flat, recursion-free execution. |
| `path_budget` | Cycle-count consistency verifier / static budget checker for deterministic hot paths. |
| `intrinsics` | Platform-agnostic wrappers for SIMD (AVX2/AVX-512), BMI, and cache control. |
| `no_recursion_check` | Static asserts and macro guards forbidding recursive function calls on the hot path. |
| `simd_engine` | Unified abstraction over AVX2/AVX-512 vector lane processing blocks. |
| `timing_invariant` | Compilation boundaries mapped to ensure cycle-exact deterministic execution. |
| `path_guard` | Compile-time directives forcing unrolled branches into linear execution limits. |
| `state_observer` | Wait-free state viewing implementation protecting readers from dirty writes. |
| `snapshot_engine` | Point-in-time SIMD memory cloner used for background state backups. |
| `vector_lane_512` | 512-bit wide explicit hardware data lane mapping. |
| `vector_lane_engine` | Data parallel processor mapping sequential algorithms to lane architectures. |
| `hw::fpga_offload` | SmartNIC abstraction routing checksums and sequence IDs to hardware FPGA. |
| `hw::intrinsics` | Deep silicon mapping for bit-twiddling (`tzcnt`, `bsf`, `clwb`). |
| `math::fixed_math` | 100% deterministic fixed-point math library avoiding x87/FPU rounding drift. |

---

## AI & Cognitive Core (`slabflux::ai::`)
High-performance inference and high-dimensional data feeding.

| Component | Description |
| :--- | :--- |
| `deterministic_ai_core` | `O(1)` SIMD-based inference engine with zero-branching state updates. |
| `cognitive_stimulus` | High-dimensional token container for AI handlers with certainty scoring. |
| `slabflux::hft::cognitive_synapse` | Specialized gateway mapping market data signals directly into the AI engine. |
| `slabflux::domain::moe_spark` | Mixture-of-Experts routing payload to handle context switching. |
| `slabflux::domain::causal_backbone` | Core chronologically ordered neural topology mapper. |

---

## Bridge & State Synchronization (`slabflux::bridge::`)
Asynchronous bridges between non-deterministic layers and the pinned RTE cores.

| Component | Description |
| :--- | :--- |
| `bridge_sync` | NUMA-local state publisher using Seqlock for consistent world-snapshots. |
| `shared_state_slab` | Wait-free shared memory container for zero-copy state exposure. |
| `shared_state_buffer` | Contiguous read-only view boundary for clustered multi-node execution matrices. |
| `engine_pulse_bridge` | Maps non-deterministic engine frames to deterministic LSN-based pulses. |
| `authoritative_bridge` | Non-blocking "truth" serialization that prevents I/O stalls during NVMe journaling. |
| `observer` | High-fidelity, cache-isolated observer for the management plane. |
| `signal_backbone` | Main arterial routing structure for inter-thread lifecycle signals. |
| `slabflux::gateway` | High-throughput ingress/egress border node securing internal deterministic state. |
| `slabflux::dist::shm_bridge` | IPC shared memory bridge for external process state synchronization. |
| `slabflux::platform::external_bridge` | FFI abstraction allowing external languages to probe the core memory. |

---

## Precision Signal Routing (`slabflux::conduit::`)
Technical signal wrappers and hardware multiplexing.

| Component | Description |
| :--- | :--- |
| `signal_multiplexer` | Hardware-level signal broadcaster distributing events at compile-time. |
| `iron_ring_buffer` | Enhanced SIMD buffer featuring software pre-fetching to hide cache latency. |
| `signal` | Envelope providing RDTSC hardware tagging and CRC32 data integrity. |
| `simd_vector` | Hardware-aligned (alignas 32/64 byte) vector signal structure. |
| `slabflux::net::multicast_egress` | UDP Multicast broadcaster node for low-latency state replication. |
| `slabflux::net::delta_broadcaster` | Dispatches sparse bit-diff changes instead of full frames. |
| `slabflux::net::delta_compressor` | Hardware-accelerated bit-packing mechanism for delta replication. |
| `slabflux::net::retransmission_buffer` | O(1) sliding window holding recent frames for loss-recovery logic. |
| `slabflux::net::nack_handler` | Engine to parse Negative Acknowledgements and fire retransmissions. |
| `slabflux::net::mesh_router` | Layer 3/4 internal mesh packet switcher. |
| `slabflux::net::bridge` | Secure gateway validating external ingress events against deterministic rules. |
| `slabflux::net::backpressure_monitor` | TCP window scale tracking to throttle egress during network spikes. |

---

## Time, Cluster & Topology
Precision synchronization and distributed system coordination.

| Component | Description |
| :--- | :--- |
| `time` & `clock_node` | The absolute, drift-corrected internal time source. |
| `hlc_clock` | Hybrid Logical Clock implementation for causal event ordering across the mesh. |
| `ptp_clock_mapper` | Hardware NIC timestamp-to-system-time mapper. |
| `clock_steerer` | Logic for continuous drift correction of the timeline. |
| `lsn_heartbeat` | Microsecond-precision stall detector for sequence numbers. |
| `causal_mesh` | Topological mapping defining connected instances. |
| `causal_header` | Bit-exact header for network events ensuring global causal order. |
| `mesh_map` | Wait-free registry for process discovery and IPC shared memory mapping. |
| `static_topology` | Compile-time hardware specification for ultra-optimized deployments. |
| `failover_orchestrator` | High-availability state manager for the cluster. |
| `fused_nexus_node` | Authoritative state anchor preventing split-brain scenarios. |
| `wire_frame_lsn` | Unit comprising a network frame payload and its associated LSN sequence number. |
| `state_replay_ring` | Specialized cluster-state container for global synchronization. |
| `cluster_orchestrator` | Central state machine orchestrating network rebinding and node life-cycles. |
| `slabflux::dist::distributed_mesh_router` | IPC/Network routing node ensuring causally consistent delivery. |
| `slabflux::dist::discovery_beacon` | Multicast presence announcer for auto-forming the cluster nodes. |
| `slabflux::dist::rdma_fabric` | Remote Direct Memory Access wrapper for sub-microsecond memory sync. |
| `slabflux::dist::causal_sequencer` | Applies strict happens-before logical ordering to cross-node events. |
| `slabflux::dist::durable_saga_orchestrator` | Distributed rollback-enabled two-phase commit manager. |
| `slabflux::dist::failover_signal` | Hardware-priority UDP heartbeat ensuring active/passive failovers. |
| `slabflux::dist::gateway_node` | Border ingress proxy securing internal node traffic. |
| `slabflux::dist::node_directory` | Static array mapping logical nodes to physical IPs. |
| `slabflux::orchestration::distributed_state_matrix` | Read-only matrix aggregating memory views from multiple machines. |
| `slabflux::sys::topology_enforcer` | Validation logic enforcing `hwloc` CPU affinities and NUMA restrictions. |
| `slabflux::sys::topology_scanner` | Initialization-time PCI/NUMA interrogator aligning system resources. |

---

## String Services (`slabflux::core::`)
Zero-allocation and chunk-based string management.

| Component | Description |
| :--- | :--- |
| `fixed_string<N>` | Stack-based, trivially copyable string container. |
| `smart_string` | Dynamic, zero-allocation string wrapper utilizing fragmented chunks. |
| `string_service` | Lifecycle manager for `smart_string` chunk pools. |
| `string_chunk` | Backend node structure representing a strictly-sized fragment of a `smart_string`. |
| `global_string_pool` | System-wide, thread-local aware lock-free allocator for string chunks. |

---

## Transport, I/O & Persistence
Linux-native zero-syscall I/O and protocol parsing.

| Component | Description |
| :--- | :--- |
| `io_uring_ingress` | High-performance Linux-native zero-syscall I/O loop. |
| `async_journal` | Asynchronous write-ahead logging (WAL) for system state. |
| `network_conduit` | TCP-based conduit operating via `bind_socket` and `poll_tx/rx`. |
| `matrix_nexus` | Protocol-agnostic entry point binding network data directly to the logical core. |
| `http_request_event` | 64-byte aligned HTTP event with AVX2 buffer. |
| `http_parser` | `O(1)` AVX2-accelerated request parser. |
| `slabflux::io::af_xdp_ingress` | Linux AF_XDP socket wrapper for kernel-bypass packet reception. |
| `slabflux::io::baremetal_egress` | Raw socket/NIC driver interface for minimal latency transmission logic. |
| `slabflux::io::eader_parser` | Rapid protocol header dissection (IPv4/UDP) inside the ingress pipeline. |
| `slabflux::io::hardware_shaper` | Configures NIC traffic control (TC) to prevent outgoing micro-bursts. |
| `slabflux::io::header_parser` | High-speed SIMD token scanner for plain-text protocol dissection. |
| `slabflux::io::mirrored_journal` | Dual-NVMe logic ensuring fault-tolerance across mirrored RAID blocks. |
| `slabflux::io::egress` | Single-threaded egress loop monopolizing a dedicated core for TCP flushes. |
| `slabflux::io::stack` | Bare-metal TCP/IP stack implementation bypassing the OS completely. |
| `slabflux::io::wire_latency_monitor` | Analyzer comparing NIC hardware PTP timestamps against software arrival. |
| `slabflux::storage::durable_journal` | Backing interface for zero-copy, append-only WAL to NVMe block devices. |
| `slabflux::storage::durable_sink` | O_DIRECT logical consumer for persistence data. |
| `slabflux::storage::durable_source` | High-speed file reader loading historical data for Replay. |
| `slabflux::storage::durable_storage` | Manager organizing raw NVMe block layouts. |
| `slabflux::hft::zero_copy_egress` | Direct DMA memory mapping construct for outgoing market orders. |
| `slabflux::hft::stall_free_nexus` | Busy-polling network ingress loop designed to completely monopolize the CPU. |
| `slabflux::hft::matrix_nexus` | Multi-cast receiver dropping packets directly into thread-local pools without locks. |
| `slabflux::net::server_ingress` | Abstract handler orchestrating client TCP socket acceptance. |
| `slabflux::net::network_replicator` | Node pushing synchronized states out to connected passive readers. |
| `slabflux::net::nexus_connector` | Ties hardware Ingress events to the internal logical mesh network. |
| `slabflux::net::snapshot_sync` | Point-to-point mass memory transfer logic for booting new nodes. |
| `slabflux::net::uring_ingress_stream` | `io_uring` tuned specifically for SQPOLL and fixed buffers in the net stack. |
| `slabflux::transport::http` | Main HTTP definitions and structural schemas. |
| `slabflux::transport::http_gateway` | Bridges bare-metal socket traffic into deterministic HTTP pipelines. |
| `slabflux::transport::http_avx` | Specialized SIMD pipeline exclusively for microsecond REST APIs. |

---

## Hardware Telemetry & Silicon Guards (`slabflux::core::`)
Low-level monitoring and system-wide isolation mechanisms.

| Component | Description |
| :--- | :--- |
| `chip_telemetry` | Collector for hardware-level arrival and processing statistics. |
| `stack_guard` | Pre-faulting mechanism for stack memory to avoid runtime page faults. |
| `fpu_shield` | Logic for "engaging" and warming up FPU/SIMD registers. |
| `backpressure_valve` | Deterministic flow regulator monitoring the LSN horizon. |
| `integrity_seal` | Hardware-level memory encryption and CRC32 validation interface. |
| `integrity_guard` | Magic-number boundary definitions generating faults on memory overruns. |
| `integrity_validator` | Verifier blocking pipeline continuation if memory corruptions are detected. |
| `sys::hardware_telemetry` | Direct reader for Performance Monitoring Counters (PMC). |
| `sys::lbr_analyzer` | Zero-overhead hardware tracer utilizing Last Branch Records. |
| `sys::smi_monitor` | OS-invisible latency detector for System Management Interrupts. |
| `sys::msr_silencer` | Model Specific Register tuner for CPU frequency/power control. |
| `sys::ecc_monitor` | Tracker for memory degradation and ECC corrections. |
| `sys::mce_listener` | Emergency handler for Machine Check Exceptions. |
| `sys::cache_partitioner` | API for Intel CAT (Cache Allocation Technology). |
| `sys::power_governor` | Controller for CPU C-states and P-states. |
| `sys::isa_guard` | CPUID-based instruction set validator. |
| `sys::layout_guard` | Compile-time cache-line alignment and memory layout verifier. |
| `sys::pcie_aer_guard` | Monitor for PCIe Advanced Error Reporting. |
| `sys::signal_shield` | POSIX signal masker and deterministic translator. |
| `sys::slab_scrubber` | Background deterministic memory zeroing service. |
| `rte::ignition_manifest` | Physical environment validation protocol (cores, memory, priorities). |
| `core::buffer_flush` | Cache-line optimized forced memory flushing utilities (`CLFLUSHOPT`/`CLWB`). |
| `core::cache_shield` | L1/L2 cache partitioning boundary definition to prevent eviction. |
| `sys::thermal_guard` | Hardware throttling detectors triggering alarms on extreme die temperatures. |
| `sys::thermal_soak` | Initial CPU spinner forcing frequencies to maximum before execution. |
| `sys::watchdog_shield` | OS watchdog (`/dev/watchdog`) feeder running pinned on isolated utility cores. |
| `core::board_monitor` | Cross-node telemetry aggregator evaluating cluster-wide hardware health. |
| `core::deterministic_policer` | Hardware-aligned token bucket rate limiter dropping excess packets. |
| `core::hole_puncher` | O(1) sequence gap analyzer waiting for missing logic frames. |
| `core::hot_path_alignment` | Macro directives aligning instructions explicitly to physical I-Cache boundaries. |
| `core::instruction_shield` | Defines explicit sections to prevent kernel swapping of logic blocks. |
| `core::liveness_watchdog` | Internal timeline monitor generating PANICs on stalled processors. |
| `core::speculative_guard` | Execution fencing mechanisms mitigating speculative branching exploits. |
| `core::symbol_guard` | Verification ensuring forbidden shared libraries (e.g. glibc) are not linked. |
| `sys::alignment_checks` | C++ constraints terminating builds if padding assumptions break. |
| `sys::audit_ledger` | Append-only non-blocking diagnostic memory region for internal fault tracking. |
| `sys::binary_seal` | Locks executables into RAM (`mlockall`) and verifies signatures. |
| `sys::blackbox_recorder` | Circular flight-data memory writing hardware exceptions to disk. |
| `sys::entropy_anchor` | CPU hardware seed collector (`RDRAND`) feeding logic generators. |
| `sys::heartbeat_monitor` | Thread stall verifier confirming pipeline continuation. |
| `sys::pps_latch` | NIC/Hardware pulse-per-second syncer aligning machine time. |
| `sys::entropy` | Highly robust, cryptographically sound pool avoiding OS entropy starving. |
| `sys::state_migrator` | Moves logic blocks physically across NUMA boundaries in response to latency. |
| `sys::telemetry_node` | SPSC-driven background compiler of statistical hardware events. |
| `sys::tick_event` | The fundamental structural engine beat that drives all logic blocks. |
| `sys::tier_guard` | Compile-time verifier ensuring architecture layers don't access banned data. |
| `sys::tpm_attestor` | Hardware interface generating cryptographic proofs of valid software boot. |
| `sys::uncore_lock` | Forces Intel Uncore/Ring-bus frequencies to absolute maximums. |
| `rte::jitter_audit` | Calculates system noise post-ignition to ensure stable C-States. |

---

## Main Execution & Control

| Component | Description |
| :--- | :--- |
| `slabflux::core::core` | Master orchestrator for boot sequence, thread pinning, and resource claiming. |
| `slabflux::core::immutable_config` & `config_bridge` | Sealed, O(1) configuration registry. |
| `slabflux::core::schema` | Static typing for configuration and cluster state layout. |
| `slabflux::core::stack` | Deterministic, pre-faulted custom call stack environment for handlers. |
| `slabflux::core::sf_node_ctx` | Context tracking Logical Sequence Numbers and commit points per thread. |
| `slabflux::core::static_config` | Compile-time environment configuration parser. |
| `slabflux::core::hotpatch_bridge` | Instruction-level code swapper for zero-downtime hot logic replacement. |
| `slabflux::workflow::state_machine` | Zero-allocation, transition-table based deterministic state machine framework. |
| `slabflux::workflow::saga` | Engine coordinating deterministic, multi-stage state compensations. |
| `slabflux::logic::logic` | Core handler integrating domain behavior models. |
| `slabflux::oracle::oracle` | Logic unit resolving external facts deterministically into the mesh. |
| `slabflux::platform::os` | Core wrapper standardizing platform features across execution nodes. |
| `slabflux::platform::fabric` | Cross-process execution environment integration setup. |
| `slabflux::platform::speculative_consensus` | Protocol confirming cluster logic agreements ahead of physical ticks. |
| `slabflux::rte::environment` | Complete standalone namespace wrapper executing the event loop. |
| `slabflux::rte::fractal_node` | Concept object allowing whole environments to run as nested handlers. |
| `slabflux::rte::exit` | Methodical teardown process flushing logs and releasing memory locks. |
| `slabflux::runtime::node_runtime` | Process encapsulator managing thread lifetimes and topology binds. |
| `slabflux::security::secure_ingress` | Payload screening against DDOS boundaries before queue placement. |
| `slabflux::security::judge` | Rule engine banning external IPs at the hardware kernel-bypass level. |
| `slabflux::supplemental::async_logger` | Zero-lock logging pushing char blocks to disk threads. |
| `slabflux::supplemental::blaster_node` | Synthetic load injector executing peak throughput tests. |
| `slabflux::supplemental::chaos_engine` | Framework intentionally breaking state to test orchestration failovers. |
| `slabflux::test::chaos_injector` | In-loop corruptor utilized strictly during QA verification. |
| `slabflux::supplemental::environment` | Helper encapsulator launching background non-hot-path components. |
| `slabflux::supplemental::load_generator` | Fills memory arrays with arbitrary data to force L3 cache stress. |
| `slabflux::supplemental::prometheus_exporter` | Cold-path HTTP server scraping internal counters for metrics dashboards. |
| `slabflux::supplemental::telemetry` | Wait-free aggregator accumulating counts with explicit false-sharing padding. |
| `slabflux::supplemental::telemetry_gateway` | Endpoint bridging raw node stats to unified graphing endpoints. |
| `slabflux::mgmt::management_nanoscope` | Interface tracking granular sub-millisecond latencies across boundaries. |
| `slabflux::sys::admin_interface` | CLI/RPC bindings designed for non-blocking runtime configuration updates. |
