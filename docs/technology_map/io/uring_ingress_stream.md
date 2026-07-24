# SlabFlux I/O: uring_ingress_stream (`slabflux/io/uring_ingress_stream.hpp`)
# SlabFlux I/O: io_uring Ingress Stream (`slabflux/io/uring_ingress_stream.hpp`)

## 1. Architectural Justification
The `uring_ingress_stream` is exclusively responsible for the high-velocity ingestion of byte-streams (TCP) via `io_uring`. Its primary architectural challenge is managing fragmented protocol reads without allocating temporary string buffers or shifting memory.
Ingesting TCP byte-streams efficiently requires dealing with network fragmentation and window scaling. Traditional `epoll` + `recv()` architectures force the user-space thread to guess stream boundaries and execute excessive syscalls. `uring_ingress_stream` solves this using zero-syscall asynchronous multi-shot polling.

## 2. Hardware Implementation Directives
- **Sliding Window over Mapped Memory**: The ingress engine utilizes a continuous ring-buffer mapped directly to `io_uring`'s provided buffers (`IORING_FEAT_FAST_POLL`). As data arrives via DMA, it is appended to the tail of the buffer. 
- **Zero-Copy Reassembly**: The module parses protocol boundaries (e.g., via `simd_parser`) directly against the live buffer. If a packet is split across two TCP frames, the ingress engine leaves the partial payload in place and instantly resubmits an `IORING_OP_RECV` request to fetch the remainder, completely bypassing `memcpy` realignment.
- **Multishot Reception**: Utilizes `IORING_OP_RECV_MULTISHOT` on supported modern kernels. This allows a single SQE to continually yield CQEs as new TCP stream data arrives, radically dropping the CPU cost per ingested packet.
- **Multishot Reception**: Utilizes the modern `IORING_OP_RECV_MULTISHOT` operation. The kernel repeatedly fires incoming TCP data into the application's ring without requiring a new Submission Queue Entry (SQE) for every packet.
- **Provided Buffer Rings (PBUF)**: Pre-registers a massive circular queue of memory with the kernel. The network stack's DMA engine writes TCP payloads directly into these pre-pinned user-space boundaries.
- **Cache-Line Aligned Offsets**: Maintains strict `alignas(64)` boundaries for internal stream sliding windows to ensure continuous sequential reading by the CPU.

## 3. Protocol Delegation
The moment a mathematically complete protocol boundary is identified, the byte span is wrapped in a `managed_data` pointer and injected into the deterministic `spsc_conduit`. The deterministic core processes the stream identically to a datagram, completely isolated from the complexities of TCP windowing.
## 3. Pipeline Integration
Serves as the absolute entry point for TCP workloads. It tightly couples with the `tcp_stream_defragmenter`, pushing the raw contiguous byte windows forward to be sliced into logical `sovereign_signal` envelopes without any intermediate allocation.