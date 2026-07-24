# Testing and Audit

The framework's invariants are continuously validated within a rigorous, GTest-driven C++20 harness. The overriding objective is to guarantee seamless cross-platform parity without yielding a single microsecond of Linux-specific HFT performance.

## Monolithic Test Architecture (`all_slabflux`)
To accurately simulate the OS cache warming and memory pressure inherent in live production, all unit and integration layers are fused into a singular, monolithic binary (`all_slabflux.exe`).
* Utilizes CMake's `GLOB_RECURSE` to dynamically aggregate components.
* Strict namespace and suite naming conventions are mandated to sidestep LNK1169 symbol collisions.

## OS Jitter Audit and Determinism
The `performance_audit` and `jitter_audit` suites bypass standard timing libraries, reading raw CPU cycle counts directly via the `__rdtsc()` intrinsic.
* **Warm-up Phase:** Prior to telemetry collection, 100,000 dummy iterations are executed to prime the CPU's Branch Predictor and aggressively warm the L1 instruction cache.
* **Thread Pinning:** During live telemetry, the testing thread is violently pinned to an isolated core (`SetThreadAffinityMask` / `sched_setaffinity`), and its execution priority is elevated to critical. This prevents OS scheduler preemption (Jitter) from contaminating the O(1) performance baseline.

## Cross-Platform Tests (Windows Polyfill)
Specific low-level Linux kernel interfaces (e.g., `move_pages`, `io_uring`) lack Windows equivalents.
1. **Compile-Time Exclusion:** The `CMakeLists.txt` utilizes robust `REGEX` evaluations to physically omit incompatible source files during Windows builds.
2. **Graceful Skipping:** In scenarios where the code compiles but the underlying OS emulation (polyfill) cannot mathematically satisfy the strict microsecond latency requirements, tests are dynamically flagged as yellow (`GTEST_SKIP()`), preserving a green CI/CD pipeline.
3. **Lazy Allocation Protection:** During NUMA audits, explicit page-touching routines (First-Touch policy via `volatile char` writes) are injected to physically force OS memory allocation prior to invoking the simulated `move_pages` routine.
