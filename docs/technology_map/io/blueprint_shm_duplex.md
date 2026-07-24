# Blueprint: shm_duplex.hpp

## Architectural Overview
Bidirectional shared memory IPC mechanism. Maps dual SPSC rings over a single `mmap` boundary to facilitate zero-syscall, lock-free communication between independent deterministic processes.