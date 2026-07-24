# SlabFlux Net: Invariant-Driven TCP Flow Engine (`tcp_flow_engine.hpp`)

## 1. Architectural Overview
Traditional TCP/IP stacks (Linux, BSD, lwIP) manage the TCP lifecycle (LISTEN, SYN_SENT, ESTABLISHED, TIME_WAIT, etc.) using massive, deeply nested `switch/case` state machines. In a high-frequency trading pipeline, these unpredictable branches thrash the CPU's branch target buffer and degrade instruction cache locality.

The `tcp_flow_engine` completely abandons the textbook state-diagram approach. It tracks connections using an **$O(1)$ branchless bitmask state machine** combined with rigid memory geometries, maintaining 100% RFC-compliant on-the-wire behavior without the traditional software overhead.

## 2. Bitmask-Driven State Transitions
TCP states are mapped directly to binary flags within a `phase_mask` on the `tcp_transmission_control_block` (TCB).

```cpp
enum tcp_phase_bits : uint8_t {
    PHASE_CLOSED      = 0x00,
    PHASE_LISTEN      = 0x01,
    PHASE_SYN_SENT    = 0x02,
    PHASE_ESTABLISHED = 0x08,
    // ...
};
```
State transitions are executed via bitwise `OR`/`AND` operations instead of branching. For example, receiving an ACK during the `SYN_RCVD` phase deterministically promotes the mask to `PHASE_ESTABLISHED` using a simple bitwise assignment, eliminating the need for `if (state == SYN_RCVD) { ... }` blocks in the hot path.

## 3. Structural Encapsulation & Alignment
In standard stacks, packets are built dynamically by allocating `sk_buff` or `mbuf` structures and appending headers via linked lists. This causes memory fragmentation and cache misses.

SlabFlux dictates a strict **Structural Fusion** geometry:
```cpp
template <size_t MSS = 1460>
struct alignas(64) outbound_tcp_segment {
    raw_tcp_ipv4_frame header; // Exactly 64 bytes (L1 Cache Line)
    char payload[MSS];         // Contiguous payload block
};
```
By packing the L2/L3/L4 headers into exactly 64 bytes (one cache line), the system guarantees that synthesizing outbound headers requires exactly one L1 cache transaction. The payload immediately follows, allowing NIC DMA engines to read the entire segment linearly.

## 4. Branchless AVX Checksum Synthesis
The `tcp_wire_engine` performs 1's complement TCP/IP checksum validation and generation. 
Instead of scalar loops, it utilizes auto-vectorizable 32-bit/64-bit block additions that the compiler lowers to SIMD instructions. The 64-bit accumulator is then folded into a 16-bit word branchlessly using shift-and-add arithmetic.

## 5. Security & Invariants
- **Sequence Bounding:** Ingress sequence validation (`seq - rcv_nxt <= rcv_wnd`) relies on unsigned integer underflow. Any packet outside the valid window underflows to a massive number and is instantly rejected in a single CPU cycle.
- **Zero Dynamic Allocation:** The TCB strictly points to pre-allocated, wait-free memory rings (`tx_unacked_ring`, `rx_stream_ring`). State tracking itself requires zero heap activity.