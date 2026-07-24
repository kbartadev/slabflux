# SlabFlux Net: Baremetal Gateway (`gateway.hpp`)

## 1. Architectural Overview
The `gateway` is the absolute edge router of the SlabFlux network application. Its sole responsibility is to intercept chaotic, untyped binary byte streams originating from the network hardware and instantly forge them into strictly typed C++ structures suitable for the deterministic core.

## 2. Zero-Copy Ingress
Standard network stacks parse raw byte arrays into intermediate `sk_buff` chains, deserialize them into JSON/Protobuf, and finally instantiate heap-allocated objects.
The SlabFlux gateway abandons dynamic instantiation:
- Payload bytes arriving in the AF_XDP UMEM or fixed `io_uring` buffers are never copied.
- The gateway instantly translates the raw network buffer via `reinterpret_cast` directly into C++ POD references (e.g., `const TradeTick&`).
- This eliminates dynamic serialization loops, memory fragmentation, and latency jitter, routing data at line-rate.

## 3. Jump-Table Routing (Chicago Gateway Pattern)
Protocol demultiplexing typically incurs high branching penalties (`if/else if/switch`). 
To maintain a flat execution profile, the gateway utilizes an O(1) function-pointer jump table:
- The first few bytes of the raw network frame contain a structural Type ID.
- The gateway uses this ID as a direct integer index into a contiguous array of pre-compiled pipeline entry functions.
- This instantly dispatches the raw byte structure directly to the correct handler coordinates without a single logical evaluation branch.

## 4. Memory Geometry Validation
To prevent catastrophic segmentation faults when blindly casting network buffers:
- The gateway performs a rapid bounds check to ensure the payload length matches the expected `sizeof(T)`.
- It validates cache-line alignment requirements (`alignas(64)`), ensuring the incoming data is geometrically safe for AVX-512 consumption before permitting entry into the core pipeline.