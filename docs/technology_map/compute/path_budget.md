# SlabFlux Compute: Path Budget (`path_budget.hpp`)

## 1. Architectural Overview
Determinism requires that an operation not only computes the correct mathematical result, but does so within an identical timeframe. The `path_budget` module is a micro-architectural accounting system that monitors and enforces strict CPU cycle limits for specific logic branches.

## 2. RDTSC Cycle Profiling
Every domain handler or `execution_node` is assigned a static, `constexpr` budget in CPU cycles (e.g., `constexpr size_t BUDGET_CYCLES = 250`).

- **Entry Stamp**: The dispatcher reads the hardware Time Stamp Counter (`__rdtsc()`) immediately before entering the handler.
- **Exit Stamp**: The dispatcher reads the TSC again immediately upon return.

## 3. Jitter Detection & Quarantine
If `(Exit - Entry) > BUDGET_CYCLES`:
- The system detects a severe timing anomaly (caused by a cache miss, an SMI interrupt, or an unoptimized code path).
- Repeated violations generate a Fray index > 0.
- The CPU organically jumps to the No-Op sinkhole via the Aphasic Horizon. The node is deterministically starved of execution context, smoothly yielding its role in the causal mesh without arbitration.

## 4. Automated Feedback Loop
During integration testing, the `path_budget` module is run in a "Calibration Mode." It executes the pipeline millions of times, calculates the 99.99th percentile (p99.99) latency for each individual handler, and automatically generates the static C++ headers defining the cycle budgets for production compilation.