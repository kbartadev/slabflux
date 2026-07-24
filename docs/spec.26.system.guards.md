# System Guards & Silicon Integrity

At extreme execution velocities, physical silicon is susceptible to cosmic radiation, voltage droops, and cache coherency faults. The `slabflux::core` ecosystem assumes hardware is inherently hostile and employs ruthless verification mechanisms.

## `slabflux::core::integrity_seal`
A zero-latency, hardware-accelerated memory integrity watchdog.
* **CRC32 Hardware Hashing:** Leverages the `_mm_crc32_u64` intrinsic to continuously compute rolling hashes of the `current_lsn` and deterministic state deltas.
* **State Tamper Detection:** Ensures that the bit-exact state residing in L1/L2 caches has not been corrupted by an unauthorized memory write from an unpinned thread. 

## `slabflux::core::stack_guard`
A pre-faulting mechanism executing during the `Ignition` sequence.
* **Pre-Faulting:** Invokes `pre_fault_stack(2048)` to touch all memory pages allocated to the thread stack. This guarantees that the OS kernel has definitively mapped physical RAM to the virtual addresses, fundamentally preventing catastrophic Page Faults during live market hours.
