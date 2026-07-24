# Asynchronous I/O and Journaling

To sustain unyielding determinism, the SLABFLUX is strictly forbidden from blocking on disk or network I/O operations. The `slabflux::io` namespace abstracts massive, asynchronous storage mechanisms to achieve this.

> **⚠️ MODULE STATUS: UNDER CONSTRUCTION**
> The following I/O and Journaling components are currently in the implementation phase.

## `slabflux::io::io_uring_ingress`
A Linux-exclusive, ultra-high-throughput networking and filesystem layer leveraging the modern `io_uring` kernel interface.
* **Syscall-Free I/O:** Submits read/write vectors and reaps completions exclusively via shared memory ring buffers. This completely bypasses the catastrophic context-switch overhead inherent in legacy `read()`, `write()`, or `epoll` system calls.
* **Kernel Polling:** Allows the Linux kernel to actively poll the submission queue. Consequently, the pinned hot-path thread never yields execution time or context-switches into kernel space.
* *Note:* To maintain cross-platform architectural integrity, this module is automatically stripped from Windows targets via CMake build rules.

## `slabflux::io::durable_journal`
A zero-allocation, Write-Ahead Logging (WAL) mechanism engineered for absolute persistence parity.
* **Wait-Free WAL Architecture:** Events are serialized and dispatched to the persistence journal asynchronously, without stalling the main processing pipeline. Critically, every event is mathematically guaranteed to be persisted to disk before it is fully committed to the compute engine.
* **Sequential NVMe Writes:** Deeply optimized for enterprise NVMe hardware. Ensures that all log appends are strictly sequential and physically aligned to the exact sector boundaries of the drive, maximizing IOPS.
* It serves as the primary backbone for the `authoritative_bridge`, guaranteeing that cluster state can be resurrected with zero data loss following a catastrophic hardware failure.
