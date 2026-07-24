# Platform Abstraction and Topology

One of the paramount engineering challenges of the SLABFLUX is enforcing Linux-native HFT paradigms (such as strict NUMA binding and `io_uring` bypasses) with uncompromising reliability across Windows environments.

## `slabflux::core::hardware_topology`
This static orchestrator maps the physical silicon topology and governs thread pinning at ignition.

### CPU Affinity and Priority
To ruthlessly eradicate OS scheduler noise (Jitter), the engine exclusively commands dedicated CPU cores:
* **Linux:** Processing threads are inextricably bound to silicon using `sched_setaffinity` and `pthread_setaffinity_np`.
* **Windows:** Employs `SetThreadAffinityMask` coupled with `SetThreadPriority(..., THREAD_PRIORITY_TIME_CRITICAL)` to explicitly forbid the Windows kernel from migrating the hot-path thread.

### NUMA Locality
In modern multi-socket architectures, bridging the physical distance between RAM and the executing CPU is non-negotiable.
* **`allocate_on_local_node(size)`:** Forces memory allocation strictly from the NUMA bank physically closest to the executing core.
* **`move_pages` Polyfill:** Because this granular syscall is absent in the Windows kernel, a custom emulation layer guarantees that testing pipelines execute seamlessly without mutating the underlying HFT architecture.

## Memory Alignment (Alignment Invariants)
Every critical data structure within the framework is rigorously padded and aligned using C++20 `std::hardware_constructive_interference_size`. This ensures isolation boundaries are determined by the physical properties of the target silicon, eliminating **False Sharing** and ensuring the proprietary memory layout is distinct from fixed-padding patterns found in open-source conduits.

## Windows-Specific Macros
To guarantee a pristine build environment, the RTE automatically injects the following constraints under MSVC:
* `NOMINMAX`: Suppresses the legacy `min` and `max` macros from polluting the standard namespace.
* `WIN32_LEAN_AND_MEAN`: Strips out bloated, unnecessary Windows API subsystems to dramatically accelerate compilation times.

## Hardware Monitors & Silencers
To assert "Absolute Sovereignty" over the machine, SLABFLUX deploys active, low-level hardware sentinels.
* **MCE Listener & ECC Monitor:** Directly polls CPU machine-check architecture to detect hardware bit-flips and track degrading memory DIMMs.
* **SMI Detection & MSR Control:** Intercepts hidden System Management Interrupts and forcefully writes to Model Specific Registers to lock CPU frequency states.

## Deep Dive: Hardware Topology & OS Tuning
* **Initialization:** Prior to the execution of the C++ binary, the `init.sh` deployment script rewrites the kernel parameters, applying `isolcpus` and `nohz_full` to hermetically seal the trading cores.
* **P99.9 Latency Validation:** Recognizing that OS jitter is a physical inevitability, the runtime continuously benchmarks its own execution profile. Using the `smi_monitor`, it intercepts invisible BIOS interrupts and validates that 99.9% of all operations strictly adhere to the defined `timing_invariant`.
* **NUMA First-Touch Policy:** The `hardware_topology` engine enforces a "First-Touch" allocation mandate, deliberately writing to every page boundary post-allocation to physically force the Linux memory manager to map the RAM to the local NUMA node.
