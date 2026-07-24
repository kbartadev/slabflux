# SlabFlux Core: Telemetry (`telemetry.hpp`)

## 1. Architectural Overview
Provides lock-free, sub-microsecond observability via out-of-band hardware tracing, substituting typical standard logging mechanisms that destroy deterministic latency.

## 2. Direct MSR Hardware Tracing
Evaluates Model Specific Registers (MSRs) like `APERF` and `MPERF`, and accesses raw `__rdtsc()` cycles to trace CPU frequency scaling logic and thread stalling events without executing `clock_gettime()` syscalls.

## 3. Zero-Stall Observability
Telemetry probes drop diagnostic markers into localized `nanoscope_bridge` buffers using entirely relaxed atomic fetch-adds. A separate, cache-isolated background thread aggregates the diagnostics, ensuring the hot path is never burdened by formatting strings or yielding to the OS.