# Blueprint: socket_duplex.hpp

## Architectural Overview
Unified, bidirectional POSIX socket wrapper that utilizes strict `epoll` state machines and batching to connect standard OS networking to the SlabFlux wait-free execution matrices.