# SlabFlux I/O: wire_latency_monitor (`slabflux/io/wire_latency_monitor.hpp`)

## 1. Architectural Justification
Understanding true wire-to-wire latency is critical for diagnosing algorithmic determinism. The `wire_latency_monitor` measures the absolute nanosecond delta between the hardware reception of an Ethernet frame and the transmission of the corresponding algorithmic response, executing completely independently of the Linux system clock.

## 2. Hardware Implementation Directives
- **RDTSC Stamping**: Ingress packets are instantly stamped with the CPU's TSC value (`__rdtsc()`) the moment they cross the `af_xdp_ingress` or `io_uring` threshold. This temporal stamp travels through the execution manifold wrapped securely inside the `sovereign_signal`.
- **Zero-Allocation Telemetry**: As the egress engine dispatches the outbound sequence, it subtracts the original stamp from the current TSC. The resulting delta is pushed wait-free into an asynchronous logging conduit (e.g., `shm_journal_duplex`), ensuring telemetry adds strictly zero bytes of dynamic allocation to the hot path.
- **Calibration and Jitter Detection**: Capable of analyzing CPU frequency scaling and cache-miss micro-stutters by correlating latency spikes against known L1 instruction cache eviction profiles.

## 3. PTP / Hardware Timestamping
Where supported by the NIC (e.g., Solarflare, Intel E810), the monitor can interface with hardware-level PTP (Precision Time Protocol) timestamps embedded directly in the packet descriptors. This offers sub-nanosecond correlation between the physical wire-arrival of the light pulse and the CPU's observation of the DMA transfer.