# SlabFlux I/O: Mirrored Journal (`slabflux/io/mirrored_journal.hpp`)

## 1. Architectural Justification
Provides user-space RAID-1 style asynchronous logging to multiple NVMe drives to guarantee fault-tolerant LSN watermarking.

## 2. Hardware Implementation Directives
- **Multiplexed Submissions**: Clones outbound state pointer references and submits parallel `IORING_OP_WRITEV` entries to independent block devices.
- **Wait-Free Consensus**: Tracks I/O completions independently. The execution mesh is notified of durable state only after all mirrored drives have reported successful CQEs.
- **Non-blocking Resiliency**: If one NVMe drive stutters, it does not halt the algorithm; the system gracefully manages parallel window bounds until quorum is reached.