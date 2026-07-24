# SlabFlux Sys: Hardware Telemetry (`hardware_telemetry.hpp`, `lbr_analyzer.hpp`)

## 1. Architectural Overview
In nanosecond-scale engineering, software profilers (like `perf` or `gprof`) introduce observer effects that alter the very latency they are trying to measure. The `hardware_telemetry` module interfaces directly with the CPU's physical Performance Monitoring Units (PMUs) to achieve invisible, zero-overhead tracing.

## 2. Model-Specific Registers (MSRs)
Instead of instrumenting C++ code with timing hooks, the module reads hardware counters:
- **L1/L2 Cache Misses**: Directly tracks hardware counters for eviction events, allowing engineers to pinpoint exact structural geometries that break spatial locality.
- **Branch Mispredictions**: Reads the CPU's branch predictor failure counters to identify execution paths that violate the zero-branching invariants.

## 3. Last Branch Record (LBR) Analyzer
The `lbr_analyzer` utilizes the Intel LBR hardware feature.
- The CPU automatically records the source and destination addresses of the last 32 branches in dedicated silicon registers without stalling the instruction pipeline.
- When a catastrophic divergence or latency spike triggers the `error_arbiter`, the LBR registers are dumped to the `blackbox_recorder`. This provides a perfect, cycle-accurate stack trace of the hardware execution path leading up to the crash, even without debug symbols.