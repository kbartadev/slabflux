# Blueprint: uring_duplex.hpp

## Architectural Overview
Unified bidirectional network engine. Multiplexes `IORING_OP_RECV` and `IORING_OP_SEND` within a single shared memory ring, sharing a common SQPOLL thread to eradicate kernel transitions for fully duplexed connections.