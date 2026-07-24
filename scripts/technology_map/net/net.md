# Blueprint: Network Architecture

## Architectural Overview
The SlabFlux networking architecture completely excises standard POSIX socket blocking and payload serialization by treating raw hardware NIC queues as direct memory extensions of the C++ execution pipeline.

## Core Components
- **Kernel-Bypass Ingress (`demux_gateway`)**: Exposes network buffer bytes as strongly-typed C++ events. Evaluates jump-table offsets based on raw binary ID headers at O(1) latency, injecting pointers deep into the pipeline execution path with zero copies.
- **AF_XDP Transports (`xdp_socket`, `xdp_conduit`)**: Binds strictly to ring buffers mapped directly into the NIC driver hardware queue before `sk_buff` allocation, circumventing the Linux network stack and netfilter hooks entirely.
- **Hardware Endian Translation (`endian_mask`)**: Leverages 1-cycle AVX2 `_mm_shuffle_epi8` execution instructions to byte-swap 16-byte protocol elements from Big-Endian network order to host order natively.
- **Zero-Allocation Socket Bridges (`uring_gateway`)**: Employs an explicit SQPOLL-enabled `io_uring` thread that manages submission/completion queues entirely through shared memory to achieve user-space I/O.