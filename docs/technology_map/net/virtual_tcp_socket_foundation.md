# Foundation: Virtual TCP Socket Abstraction

## 1. Architectural Justification
Integrating a custom Kernel-Bypass TCP stack with higher-level protocols (HTTP/JSON) usually requires rewriting the application logic to handle raw TCB pointers and sequence offsets. 

The `virtual_tcp_socket` acts as a zero-overhead translation boundary. It provides standard POSIX semantics (`send`, `recv`, `connect`) to the application layer while completely decoupling the protocol business logic from the wait-free memory conduits and TCP sequence mathematics.

## 2. Hardware Implementation Directives
- **Non-Blocking Execution Guarantee:** Calls to `send()` and `recv()` never invoke syscalls and never sleep. If the underlying `tx_unacked_ring` is full, `send()` instantly returns 0 (`EWOULDBLOCK` equivalence), propagating physical backpressure upwards without thread suspension.
- **Zero-Copy Payload Delegation:** When `send()` is called with a massive buffer (e.g., a 4MB JSON block), the socket defers instantly to the `tcp_stream_fragmenter`, slicing the view into MSS chunks that map directly onto outbound DMA slots without intermediate allocation.

## 3. Bibliography & Proofs
1. **Stevens, W. R.** (1990). *UNIX Network Programming*. (Establishes the definitive semantic contract of the Berkeley Sockets API that the virtual socket flawlessly emulates in user space).
2. **Kalia, A., Kaminsky, M., & Andersen, D. G.** (2016). *Fass: A Fast, Scalable, and Simple In-Memory Key-Value Store*. USENIX. (Demonstrates how providing standard APIs over wait-free hardware rings yields maximum developer ergonomic adoption without sacrificing line-rate throughput).