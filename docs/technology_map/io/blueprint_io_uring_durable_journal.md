# Blueprint: io_uring_durable_journal.hpp

## Architectural Overview
Implements an event-sourcing persistence layer. Utilizes asynchronous, kernel-bypass block I/O (`O_DIRECT` + `io_uring`) to flush deterministic state to NVMe storage without blocking the compute matrix.