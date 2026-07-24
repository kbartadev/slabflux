# SlabFlux Sys: Power Governor (`power_governor.hpp`, `uncore_lock.hpp`)

## 1. Architectural Overview
Modern CPUs aggressively attempt to save power by dynamically scaling voltages and frequencies (P-States) and putting idle cores to sleep (C-States). Waking a core from C6 sleep can take over 50 microseconds—an eternity in HFT. The `power_governor` surgically disables these hardware power-saving features.

## 2. C-State and P-State Lockdown
During the `ignition_manifest`:
- The governor interfaces with `/dev/cpu_dma_latency` to set the maximum allowed latency to 0, signaling the OS kernel to never allow the processor to enter deep sleep states.
- It locks the performance governor to `performance`, forcing the CPU to remain at its maximum turbo frequency (P0) permanently.

## 3. Intel Uncore & Ring-Bus Locking
Even if the CPU cores are running at maximum frequency, the interconnect ring-bus (Uncore) that transfers data between the L3 cache and the PCIe lanes might downclock.
- The `uncore_lock` module writes directly to Intel MSRs (`MSR_UNCORE_RATIO_LIMIT`) to lock the mesh/ring-bus frequency to its maximum multiplier.
- This mathematically guarantees that DMA transfers from the NIC (via AF_XDP or io_uring) to the L3 cache maintain a perfectly flat, deterministic latency profile, regardless of the overall system load.