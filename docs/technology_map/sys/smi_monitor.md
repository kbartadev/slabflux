# SlabFlux Sys: SMI Monitor (`smi_monitor.hpp`)

## 1. Architectural Overview
System Management Interrupts (SMIs) are hardware-level interrupts that preempt the entire CPU—bypassing the OS kernel entirely—to run motherboard firmware tasks (like thermal regulation or ECC memory scrubbing). An SMI can freeze a CPU core for 100+ microseconds, which is lethal in a deterministic execution environment.

## 2. MSR Polling and Detection
The `smi_monitor` utilizes Intel/AMD Model Specific Registers (MSRs), specifically `MSR_SMI_COUNT` (0x34).
- A background housekeeping thread periodically reads this physical hardware counter using the `rdmsr` instruction.
- If the counter increments, it mathematically proves that the CPU die was seized by the motherboard firmware.

## 3. High-Frequency Emulation Detection
Because reading MSRs requires Ring 0 (root) privileges, the monitor can alternatively operate purely in user-space via a tight spin-loop:
- The monitor thread executes back-to-back `__rdtsc()` instructions.
- Because two consecutive `__rdtsc()` calls should resolve within a few dozen cycles, any massive delta (e.g., 50,000 cycles) instantly exposes the invisible SMI freeze.

## 4. Orchestration Integration
If SMIs occur at a frequency exceeding acceptable tolerances, the monitor signals the `failover_orchestrator`. The cluster can migrate the active workload to a secondary node residing on a completely different physical chassis to escape the faulty hardware.