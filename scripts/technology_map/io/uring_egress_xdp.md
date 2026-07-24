# Blueprint: uring_egress_xdp.hpp

## Architectural Overview
Forces physical network frame transmission directly onto the NIC rings, bypassing standard POSIX socket mechanisms entirely to obtain nanosecond-level outbound determinism.

## Core Logic & Mechanisms
- **TX Ring Population**: The `poll_egress` loop extracts items from the `spsc_conduit` and injects them onto the `xsk_ring_prod` TX ring, issuing the hardware transmit signal directly.
- **Completion Ring Cleanup**: Unrolls the `xsk_ring_cons` ring sequentially to acknowledge transmitted frames, instantly executing `pool.release_batch` to free memory allocations.
- **Syscall Omission**: Eliminates all `sendto()` or `sendmmsg()` usage, communicating purely via cache-aligned memory barriers synced with the hardware driver.