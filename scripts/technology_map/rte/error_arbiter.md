# SlabFlux Compute: Error Arbiter (`error_arbiter.hpp`)

## 1. Architectural Overview
Standard C++ applications handle critical errors by throwing exceptions or calling `std::terminate()`. In a distributed high-frequency trading (HFT) mesh, taking down the entire process due to a single malformed packet destroys global state determinism and invites systemic collapse.

The `error_arbiter` is a centralized, deterministic authority responsible for intercepting, auditing, and routing fatal computational invariants without unrolling the execution stack or halting the primary processor thread.

## 2. In-Band Threat Isolation
When a hardware check (like `numerical_sanitizer` or `temporal_guard`) breaches an invariant, it signals the `error_arbiter`.
- Instead of crashing, the arbiter packs the failure telemetry (the LSN, the specific `fault_severity`, and a 32-bit CRC hash of the corrupted state) into an atomic 16-byte `fault_record`.
- This record is pushed wait-free into the `sys::audit_ledger` and `sys::blackbox_recorder`.

## 3. Graceful Degradation and Demotion
The arbiter dictates the system's fate based on the `fault_severity` matrix:
- **WARNING**: Drops the corrupted event or SIMD lane. The pipeline execution continues unimpeded.
- **CRITICAL**: The arbiter demotes the active `pipeline` into a restricted "Safe Mode." New incoming orders are aggressively rejected at the gateway, but open states and existing positions are safely liquidated or frozen.
- **PANIC**: Triggers the `failover_orchestrator`. The node instantly drops its active network heartbeat, forcing the secondary passive replica (which possesses the uncorrupted causal state) to instantly take over via UDP IP-spoofing.

## 4. Absolute Diagnostic Purity
Because the `error_arbiter` guarantees the hot-path never physically crashes (avoiding kernel core-dumps), the memory layout and CPU cache states remain entirely intact for external telemetry probes. This enables engineers to perform flawless post-mortem analyses on the exact silicon conditions that precipitated the failure.