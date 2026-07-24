# Blueprint: shm_ingress.hpp

## Architectural Overview
Unidirectional IPC reception node. Spins on memory-mapped lock-free queues to ingest incoming cross-process state pointers directly into the application's deterministic conduit.