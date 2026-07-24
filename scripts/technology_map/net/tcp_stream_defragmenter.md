# SlabFlux Net: TCP Stream Defragmenter (`tcp_stream_defragmenter.hpp`)

## 1. Architectural Overview
TCP is a stream-based protocol, not a message-based one. A single `recv()` call can return a partial message, multiple messages, or one-and-a-half messages. The `tcp_stream_defragmenter` is a high-performance, zero-allocation reassembly engine that reconstructs fixed-size logical frames from a chaotic TCP byte stream.

## 2. Zero-Copy Buffer Management
Traditional defragmenters copy partial data into dynamic heap-allocated buffers, introducing latency and memory fragmentation.

The SlabFlux defragmenter operates on a pre-allocated, thread-local circular buffer (`reassembly_buffer_`):
- **Direct Ingress**: Raw bytes from the `io_uring` or `AF_XDP` ingress are copied directly into the ring.
- **Sliding Window View**: The parser maintains a `cursor_` that slides along the buffer. It does not modify or move the underlying data.

## 3. State Machine Parsing
The defragmenter uses a simple, branchless state machine to find message boundaries:
1. **`AWAIT_HEADER`**: The parser scans for the magic bytes of the next `wire_frame` header.
2. **`AWAIT_PAYLOAD`**: Once the header is found, it reads the embedded `payload_length`. The parser then waits until the number of bytes available in the ring buffer (`write_cursor - read_cursor`) is greater than or equal to `payload_length`.
3. **Dispatch**: When a full frame is available, the parser does not copy it. Instead, it `reinterpret_cast`s a pointer directly to the frame's location within the circular buffer and dispatches this pointer into the `pipeline`.
4. **Consume**: After dispatch, the `read_cursor` is advanced by the frame length, logically "consuming" the data without physically zeroing it out.

## 4. Performance and Security
- **SIMD Scanning**: The search for the header's magic bytes is accelerated using AVX2/AVX-512 `_mm256_cmpeq_epi8` instructions, allowing the parser to scan 32 bytes of the stream per clock cycle.
- **Backpressure**: If the reassembly buffer becomes full (indicating a downstream processing bottleneck or a DoS attack), the defragmenter stops reading from the TCP socket, allowing TCP's native flow control to signal backpressure to the remote sender.