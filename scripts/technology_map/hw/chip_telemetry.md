# Blueprint: chip_telemetry.hpp

## Architectural Overview
Abandons `std::chrono` API abstractions to read raw CPU hardware timestamps, providing true sub-nanosecond, zero-syscall latency observations.

## Core Logic & Mechanisms
- **TSC Ingestion**: Interrogates the CPU Time Stamp Counter exclusively via the `__rdtsc()` intrinsic.
- **MSR Frequency Scaling Evaluation**: Exposes pathways to read APERF and MPERF Model-Specific Registers, determining exact hardware clock-speed adjustments in real-time.