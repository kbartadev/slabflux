# Blueprint: uring_egress_stream.hpp

## Architectural Overview
Aggregates outbound deterministic state events into batched `IORING_OP_WRITEV` scatter-gather submissions for connection-oriented TCP streams, mitigating micro-writes and TCP Nagle latencies.