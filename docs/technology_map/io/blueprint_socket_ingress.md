# Blueprint: socket_ingress.hpp

## Architectural Overview
Fallback ingress layer for POSIX networking. Uses edge-triggered `epoll` multiplexing and non-blocking `recv()` bursts to map synchronous sockets into asynchronous, zero-allocation `spsc_conduit` event streams.