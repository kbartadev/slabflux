# Blueprint: mirrored_journal.hpp

## Architectural Overview
User-space RAID-1 style asynchronous logging. Multiplexes `io_uring` writes across multiple NVMe drives to guarantee fault-tolerant LSN watermarking without computational blocking.