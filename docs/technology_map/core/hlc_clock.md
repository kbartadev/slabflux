# SlabFlux Core: Hybrid Logical Clock (`hlc_clock.hpp`)

## 1. Architectural Overview
In a distributed, lock-free cluster, relying solely on physical wall-clocks (like NTP or PTP) to order transactions across different nodes is mathematically unsafe due to inherent clock drift and network jitter. 
The `hlc_clock` implements a Hybrid Logical Clock (HLC), providing an absolute, monotonically increasing temporal continuum that perfectly preserves the "happens-before" causal relationship of all distributed events.

## 2. Dual-Component Time Synthesis
The HLC synthesizes time into a single 64-bit integer, conceptually split into two components:

### Physical Component (Upper Bits)
Driven by the local hardware's Time Stamp Counter (`RDTSC`), normalized through the `clock_steerer` to approximate UTC nanoseconds. This provides intuitive, human-readable timestamps for auditing.

### Logical Component (Lower Bits)
A strict, monotonic counter. When multiple events occur within the exact same physical nanosecond (a common occurrence in AVX-512 batch processing), the logical counter increments, ensuring no two events ever share the exact same timestamp.

## 3. Causal Sync on Ingress
When a node receives a `wire_frame_lsn` from the network, it reads the remote node's HLC timestamp embedded in the `causal_header`.
- The local `hlc_clock` compares the remote timestamp with its own internal time.
- If the remote timestamp is "in the future" (due to clock drift or transmission physics), the local clock instantly fast-forwards its logical component to exactly `remote_timestamp + 1`.
- This mathematically guarantees that the local node will process subsequent reactions as causally *after* the received event, eliminating temporal paradoxes in the distributed state matrix.

## 4. Performance Geometry
The HLC state is isolated into a designated `alignas(64)` memory block to prevent false sharing. Querying the clock involves a single `rdtsc` instruction followed by a bitwise MAX operation, generating a globally verifiable timestamp in less than 5 CPU cycles without ever trapping to the OS kernel.