# Blueprint: io_uring_ingress.hpp

## Architectural Overview
Near-kernel-bypass reception engine leveraging the Linux `io_uring` subsystem. Utilizes SQPOLL threads and pre-registered memory buffers to process standard POSIX sockets with zero system calls and zero payload copies.