# SlabFlux Core: Fused Nexus Node (`fused_nexus_node.hpp`)

## 1. Architectural Overview
The `fused_nexus_node` handles kernel-bypass network ingress. It composes the `server_ingress` for lifecycle management while deploying custom Multishot Buffer Rings (`io_uring_buf_ring`) for true zero-copy, zero-syscall network ingestion.

## 2. Zero-Copy Hardware Pinning
The Nexus Node binds directly to a `pinned_allocator_spsc`. 
- The DMA memory pool is mapped straight into the `io_uring` kernel space.
- When a packet arrives, the kernel drops it perfectly into the pre-allocated C++ struct without any user-space memory copying.

## 3. SQPOLL and Core Affinity
To achieve zero-syscall polling, the Nexus Node configures `io_uring` with `IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF`. The kernel spins a dedicated polling thread on a physically adjacent CPU core, entirely offloading the system-call overhead from the primary trading loops.

## 4. Telemetry and Saturation
If the downstream logic is saturated, the Nexus gracefully drops the frame and tracks the anomaly via the `full_drop_count_`. It supports seamless fallback to the Aphasic Horizon to route saturation errors deterministically without crashing.