# SlabFlux Transport: Stateful Baremetal Parser (`baremetal_parser.hpp`)

## 1. Architectural Overview
While `http_avx_parser` provides supreme throughput for contiguous internal networks, the `baremetal_parser` is designed to face the hostile public internet. It is a strictly **Stateful, Resumable Parser** built to handle adversarial TCP fragmentation (e.g., Slowloris DDoS attacks) where an HTTP request might arrive one byte at a time.

## 2. Absolute Resumability
If the parser reaches the end of the TCP buffer in the middle of a token (e.g., `Content-Len`), it yields `parser_status::INCOMPLETE`. It stores its exact `parser_state` and consumed byte offsets in an external `http_frame` context.

When the `tcp_stream_defragmenter` provides the next packet, the parser resumes execution flawlessly without rescanning previously validated bytes.

## 3. Computed Gotos (Threaded Code)
To achieve high performance despite maintaining complex state machinery, `baremetal_parser` discards the standard `switch(state)` paradigm in favor of **Computed Gotos**:

```cpp
static const void* const dispatch[] = {
    &&L_METHOD, &&L_URI, &&L_VERSION, &&L_HEADER_KEY, ...
};
goto *dispatch[(uint8_t)frame.internal_state];
```
This acts as a hardware-level instruction router. By jumping directly to memory addresses containing the execution blocks, it entirely shields the CPU's Branch Predictor from state-machine loop thrashing.

## 4. Chunked Transfer Encoding
Unlike the single-shot parsers, the `baremetal_parser` natively supports `Transfer-Encoding: chunked`.
- It maintains distinct `CHUNK_SIZE` and `CHUNK_DATA` states.
- It tracks `accumulated_body_size` across fragmented deliveries, protecting the backend memory pools from Chunk Accumulation DoS attacks by violently halting if the payload exceeds `MAX_BODY_SIZE`.

## 5. SIMD Integration
Even as a stateful DFA, the parser natively embeds AVX-256 and AVX-512 hardware intrinsics. State transitions are accelerated by feeding the buffer tails into `find_delimiter` and `find_two_chars`, seamlessly blending stream durability with hardware velocity.