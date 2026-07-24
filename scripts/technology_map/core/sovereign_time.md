# SlabFlux Core: Sovereign Time (`sovereign_time.hpp`)

## 1. Architectural Overview
Operating system clocks (`CLOCK_REALTIME`) are subject to NTP drift, leap-second smearing, and context-switch jitter. For a deterministic causal mesh, standard time APIs are unacceptable. The `sovereign_time` module establishes an absolute, monotonically increasing internal timeline insulated from all OS anomalies.

## 2. Hybrid Logical Clocks (HLC)
To establish a globally consistent event order across physical network boundaries, the system implements an HLC (`hlc_clock.hpp`).
- **Physical Component**: Driven by the raw hardware Time Stamp Counter (`RDTSC`).
- **Logical Component**: An incremental counter attached to network frames (`causal_header`).

When a node receives a message from the cluster, it compares the message's timestamp to its local clock. If the local clock is behind, the logical component is bumped, ensuring that causality (the "happens-before" relationship) is perfectly preserved without requiring atomic physical clock synchronization.

## 3. Hardware PTP Alignment
While the internal engine relies on RDTSC, financial compliance (MiFID II / CAT) requires sub-microsecond synchronization with UTC.

### The Clock Steerer
The `ptp_clock_mapper` reads Precision Time Protocol (PTP) hardware timestamps directly from the NIC's physical registers (PHC). 
- The `clock_steerer` continuously calculates the drift gradient between the CPU's `RDTSC` and the NIC's PTP time.
- Instead of aggressively stepping the clock (which breaks determinism), the steerer smoothly slews the conversion multiplier (TSC-to-Nanoseconds) over millions of cycles, ensuring a seamless, monotonic timeline.

## 4. Sub-Microsecond Execution Budgeting
The `clock_node` is intrinsically tied to the `temporal_guard`. By possessing an unwavering timeline, the execution engine can mathematically budget its execution. If a specific pipeline handler takes 400 nanoseconds instead of the budgeted 200, the `sovereign_time` module flags it instantly, maintaining the ironclad predictability of the system.