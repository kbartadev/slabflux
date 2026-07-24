# Foundation: Hybrid Logical Clock (`slabflux/core/hlc_clock.hpp`)

## 1. Architectural Justification
In a distributed cluster, relying solely on physical wall-clocks (like NTP or PTP) to order transactions is mathematically unsafe due to inherent clock drift and network jitter. The `hlc_clock` provides an absolute, monotonically increasing temporal continuum that preserves causal boundaries independent of hardware drift.

## 2. Hardware Implementation Directives
- **Dual-Component Time Synthesis**: Encodes a 64-bit integer combining the local hardware Time Stamp Counter (`RDTSC` - physical) with a strict monotonic sequence counter (logical).
- **Causal Sync on Ingress**: When a remote packet arrives containing an HLC timestamp in the "future," the local clock instantly fast-forwards its logical component to `remote_timestamp + 1` via lock-free atomic `std::max`.
- **Cache Isolation**: The HLC state is pinned into an `alignas(64)` memory block. Generating a globally verifiable timestamp consumes less than 5 CPU cycles, entirely bypassing OS kernel timer (`clock_gettime`) interruptions.

## 3. Bibliography & Proofs
1. **Kulkarni, S., et al.** (2014). *Logical Physical Clocks and Consistent Snapshots in Globally Distributed Databases*. Technical Report. (HLC algorithmic proofs).
2. **Corbett, J., et al.** (2012). *Spanner: Google’s Globally-Distributed Database*. OSDI. (TrueTime and the physical limits of clock synchronization).
3. **Intel Corporation**. *Intel SDM Vol 3A*. (Invariant TSC and RDTSC latencies).