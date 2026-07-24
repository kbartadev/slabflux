# SlabFlux Core: Liveness Watchdog (`liveness_watchdog.hpp`)

## 1. Architectural Overview
In a system that relies on spinning, thread-pinned, lock-free cores, a thread stall is a catastrophic failure. Since the execution paths intentionally bypass the OS scheduler, standard kernel-level stall detection is insufficient. 

The `liveness_watchdog` is an internal, hardware-backed timeline monitor that oversees the progress of the deterministic execution pipeline, generating PANICs or failover signals the moment a processor stalls.

## 2. Microsecond Precision Monitoring

### Time Stamp Counter (RDTSC) Tracking
The watchdog does not rely on OS sleep/wake timers. Instead, it reads the CPU's intrinsic Time Stamp Counter (`RDTSC`). 
During every cycle of the main event loop, the executing core "pets" the watchdog by submitting its current Logical Sequence Number (LSN) and the exact TSC value.

### Out-of-Band Verification
The watchdog verification logic typically runs on a dedicated, isolated housekeeping core. It continuously polls the shared memory signatures updated by the hot-path cores. 
- If the `delta` between the current TSC and the hot-path's last reported TSC exceeds the strict cycle-budget threshold (e.g., indicating a 50-microsecond stall), the watchdog engages the `error_arbiter`.

## 3. Failure Mitigation and Orchestration

### System-Wide Fencing
When a stall is detected (due to an infinite loop, memory bus lockup, or unexpected hardware interrupt), the watchdog initiates a "Shootdown". It issues cross-core memory barriers or NMI (Non-Maskable Interrupts) to fence the locked core, preventing it from emitting corrupted or delayed network packets to external systems.

### Failover Orchestration Integration
The watchdog instantly communicates with the `failover_orchestrator`. If the primary node is deemed mathematically unresponsive, the watchdog drops the high-availability heartbeat, forcing the secondary passive node to instantly take over the IP/MAC addresses via ARP spoofing and resume the causal mesh sequence.

## 4. Operational Invariants
- **Zero Overhead Hot-Path**: Petting the watchdog involves a single non-atomic memory write to an exclusively owned cache line. There are no locks, memory barriers, or cache contentions.
- **Immunity to OS Jitter**: Because the verification operates strictly on relative CPU cycles, it is immune to NTP clock steps, leap seconds, or OS context-switch jitter.