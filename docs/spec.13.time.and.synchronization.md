# Precision Time & Synchronization

In High-Frequency Trading and industrial robotics, absolute time precision is a first-class citizen.

## `slabflux::core::hlc_clock`
An enterprise-grade implementation of Hybrid Logical Clocks (HLC).
* **Causal Tracking:** Mathematically fuses the absolute synchronization of physical clocks (NTP/PTP) with the topological ordering of logical clocks (Lamport timestamps).
* **Strict Monotonicity:** Irrevocably guarantees that time always advances forward. Even if the underlying OS scheduler forcefully rewinds the system clock, the RTE timeline remains pristine and uncorrupted.

## `slabflux::core::ptp_clock_mapper` & `pps_latch`
* **Precision Time Protocol (PTP):** Bypasses the OS entirely to map the hardware-level clock of the NIC (Network Interface Card) directly to the isolated `sovereign_time` of the RTE.
* **Pulse Per Second (PPS):** Interfaces with hardware latches to synchronize multiple bare-metal machines with sub-microsecond, nanosecond-tier accuracy.

## `slabflux::core::clock_steerer`
A continuous synchronization daemon that actively micro-adjusts the logical clock rate to maintain parity with an authoritative external source (e.g., a GPS atomic clock). Crucially, it executes these corrections via sluing rather than stepping, preventing catastrophic "time jumps" that would otherwise violate O(1) execution invariants.
