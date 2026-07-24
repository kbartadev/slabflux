# SlabFlux Net: State Streamer (`state_streamer.hpp`)

## 1. Architectural Overview
The `state_streamer` provides an asynchronous, cold-standby state archival mechanism. It streams massive memory arrays (like full Retransmission Buffers) to NVMe block storage to assist in new node bootstrapping.

## 2. Zero-Blocking Background Archival
Standard file I/O operations block the active thread. The streamer leverages `io_uring` completely out-of-band:
- It utilizes `IORING_SETUP_SQPOLL` to instruct the Linux kernel to run a polling thread on an isolated core.
- When `archive()` is called, it prepares the Write SQE and submits it, allowing the hot path to resume immediately.
- Memory synchronization is handled purely by the kernel's background DMA.

## 3. Aphasic Submission Tracking
If the ring saturates because the NVMe bus is blocked, the streamer checks `io_uring_get_sqe()`.
If unavailable, it falls back to Teleological Agnosia (`execute_void_stream`), logging an `0x51` fault to the `semiotic_tapestry`. This ensures the network replication matrix prefers dropping background historical archives rather than halting the active tick propagation.