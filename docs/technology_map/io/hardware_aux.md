# SlabFlux I/O: Hardware Auxiliary (`slabflux/io/hardware_aux.hpp`)

## 1. Architectural Justification
To achieve true determinism and microsecond-level predictability, the operating system's default behavior must be suppressed. The `hardware_aux` module provides a privileged C++ API for silicon-level system isolation, ensuring the deterministic engine is never interrupted by the OS scheduler.

## 2. Hardware Implementation Directives
- **Thread Pinning**: Binds specific execution DAG nodes to isolated CPU cores using `sched_setaffinity`, ensuring hot loops stay bound to their dedicated L1/L2 caches.
- **C-State Locking**: Interacts with Model-Specific Registers (MSRs) and power management APIs to lock the CPU at maximum frequency. This prevents C-state transitions (sleep states) that introduce microsecond latency spikes upon wakeup.
- **Interrupt Shielding**: Programmatically verifies or configures kernel parameters to migrate hardware interrupts (IRQs) away from the dedicated SlabFlux CPU cores, achieving a fully tickless (`nohz_full`) environment.

## 3. Pipeline Integration
As a foundational hardware enabler, `hardware_aux` is invoked exclusively during the bootstrap phase of the SlabFlux application manifold. It establishes the strict environmental sandbox before any network I/O or SPSC conduits are initialized, guaranteeing that the runtime operates in a completely isolated, deterministic vacuum from the first tick.