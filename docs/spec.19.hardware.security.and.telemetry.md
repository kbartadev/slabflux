# Hardware Security, Integrity & Telemetry

High-stakes industrial systems and HFT engines cannot afford silent data corruption or unauthorized memory introspection. The framework employs deep hardware telemetry to continuously interrogate the physical silicon, ensuring the execution environment remains uncompromised.

> **⚠️ HARDWARE SENTINEL STATUS: PLANNED**
> Silicon-level monitoring and hardware integrity seals require direct MSR and PMC access, which is currently in active development.

## Confidential Computing & Integrity
* **`tdx_seal.sh` & `integrity_seal.hpp`**: Deep integration with Intel TDX (Trust Domain Extensions) to encrypt the application's memory pages directly at the hardware memory controller level. This guarantees that even a fully compromised OS kernel, hypervisor, or root-level actor cannot read or tamper with the SLABFLUX runtime state.
* **`signal_shield.hpp`**: Ruthlessly masks POSIX signals (`SIGINT`, `SIGTERM`, `SIGHUP`) via `sigprocmask`. Instead of invoking an asynchronous OS signal handler, which could catastrophically corrupt lock-free memory structures mid-mutation, it safely queues and translates these signals into deterministic events handled synchronously by the `error_arbiter`.

## Silicon Sentinels
* **`mce_listener`**: Directly monitors the CPU's Machine Check Architecture (MCA) registers. It intercepts fatal hardware errors (e.g., cosmic-ray-induced bit-flips in the L1/L2 caches) and initiates emergency memory flushing before the silicon halts.
* **`ecc_monitor`**: Continuously tracks memory controller registers for Correctable and Uncorrectable ECC (Error-Correcting Code) faults. By analyzing correctable error rates, it statistically predicts impending physical RAM failures and aggressively triggers the `failover_orchestrator`.
* **`smi_monitor`**: System Management Interrupts (SMIs) operate at Ring -2 (BIOS/UEFI level) and are completely invisible to the Linux OS. This monitor measures unexpected micro-gaps in the `rdtsc` cycle count to detect, log, and alert on SMI-induced latency spikes.
