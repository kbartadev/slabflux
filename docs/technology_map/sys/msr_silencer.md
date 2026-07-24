# SlabFlux Sys: MSR Silencer (`msr_silencer.hpp`)

## 1. Architectural Overview
Modern processors attempt to "intelligently" manage workloads via hardware prefetchers and opportunistic power states. In deterministic environments, this "intelligence" causes uncontrollable latency jitter. The `msr_silencer` physically disables these hardware features by writing directly to Intel/AMD Model-Specific Registers (MSRs).

## 2. Hardware Prefetcher Lockdown
By default, the CPU analyzes memory access patterns and aggressively pulls data into the L2/L3 caches. While beneficial for general workloads, in a ring-buffer architecture (`mpmc_conduit`), aggressive prefetching pulls adjacent, locked cache-lines, inducing massive False Sharing penalties.
- The `msr_silencer` modifies `MSR_PREFETCH_CONTROL` (0x1A4 on Intel).
- It explicitly disables the L2 Hardware Prefetcher, DCU Streamer, and DCU IP Prefetcher for the isolated trading cores.
- This ensures that the CPU *only* fetches memory lines explicitly requested by software (`_mm_prefetch(..., _MM_HINT_T0)`), guaranteeing absolute cache sovereignty.

## 3. C-State Autonomy
The silencer overrides the OS's `intel_idle` or `acpi_idle` drivers by directly writing to `MSR_PKG_CST_CONFIG_CONTROL`. It permanently locks the processor package into the C0 state, mathematically forbidding the silicon from lowering its voltage or parking execution ports during microsecond polling lulls.