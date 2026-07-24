# Blueprint: uring_ingress_stream.hpp

## Architectural Overview
High-velocity ingestion engine for TCP byte-streams using `io_uring` sliding windows and `IORING_OP_RECV_MULTISHOT`. Reassembles fragmented payloads in-place directly against DMA-mapped memory.