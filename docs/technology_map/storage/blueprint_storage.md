# Blueprint: Asynchronous Storage

## Architectural Overview
Persistent storage engine for event sourcing. Leverages `O_DIRECT` and `io_uring` to flush continuous state streams directly to NVMe block devices without interrupting the real-time compute thread.