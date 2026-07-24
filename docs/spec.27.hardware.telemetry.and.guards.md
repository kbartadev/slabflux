# Hardware Telemetry & Silicon Guards

A deterministic engine must self-audit its physical execution budget. The SLABFLUX telemetry plane operates out-of-band to prevent instrumentation overhead from contaminating the hot path.

## `slabflux::compute::temporal_guard`
An in-band physical time budget enforcer.
* **Cycle-Accurate Budgeting:** Continuously evaluates the physical passage of time (`__rdtsc()`) between processed events.
* **Deterministic Panics:** If the execution gap exceeds the pre-allocated CPU cycle budget (e.g., taking more than 3,000,000 cycles for a 1ms tick at 3GHz), it triggers a deterministic PANIC via the `error_arbiter` (`0xDEAD71C5`). This ensures identical behavior during offline replay validation.

## `slabflux::supplemental::telemetry_wrapper`
* **Zero False-Sharing Profiling:** Captures arrival rates and processing statistics. The underlying `chip_telemetry` is architected to ensure background scraper threads can read counter metrics without inducing cache-line bouncing or MESI protocol stalls against the pinned compute core.
