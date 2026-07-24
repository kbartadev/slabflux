# SlabFlux I/O: socket_egress (`slabflux/io/socket_egress.hpp`)

## 1. Architectural Justification
The `socket_egress` component acts as the baseline boundary bridge that flushes outbound deterministic state over standard POSIX network interfaces. It connects the lock-free application `spsc_conduit` directly to network file descriptors (TCP/UDP sockets) while maintaining strict isolation guarantees.

## 2. Hardware Implementation Directives
- **Vectorized Writes**: Leverages `writev()` or `sendmmsg()` system calls to transmit multiple independent buffer pointers in a single kernel context switch, radically amortizing the system call penalty across large event bursts.
- **Non-Blocking Backpressure Handling**: Operates strictly on sockets initialized with `O_NONBLOCK`. If the kernel's internal network buffers saturate (yielding `EWOULDBLOCK`), the egress engine dynamically absorbs the spike within its multi-megabyte user-space ring without stalling the primary compute thread.
- **Non-Temporal Cache Evasion**: Utilizes software prefetching and explicit cache-control directives where possible to ensure outbound network copies do not evict the primary algorithmic state arrays from the processor's highest cache tiers.

## 3. Buffer Recycling
The loop guarantees zero dynamic allocation. Memory blocks written to the socket are immediately handed back to the memory pool `return_conduit`, closing the memory lifecycle cycle symmetrically.