# SlabFlux Net: TCP Wire Engine & Encapsulation (`tcp_wire_engine.hpp`)

## 1. Architectural Overview
Standard operating systems assemble network packets dynamically. They allocate `sk_buff` (Linux) or `mbuf` (BSD) structures on the heap, and link various protocol headers (Ethernet, IP, TCP) via pointer chains. This fragmentation destroys spatial locality and guarantees L1 cache misses during transmission.

The `tcp_wire_engine` is a pure structural overlay. It enforces a strict memory geometry where all Layer 2, Layer 3, and Layer 4 headers are fused into a single contiguous block.

## 2. 64-Byte Structural Fusion
```cpp
struct alignas(64) raw_tcp_ipv4_frame {
    // Ethernet II (14 bytes)
    // IPv4 (20 bytes)
    // TCP (20 bytes)
    uint8_t reserved_padding[10]; 
};
```
A standard Ethernet + IPv4 + TCP header sequence (without options) is exactly 54 bytes. 
SlabFlux explicitly pads this structure with 10 bytes and enforces `alignas(64)`. 

This guarantees that the entire header matrix occupies **exactly one L1 Cache Line**. When the `tcp_flow_engine` prepares an outbound packet, the CPU writes all header fields in a single hardware transaction. The DMA engine of the NIC can then stream the headers and the contiguous payload without scatter-gather overhead.

## 3. Branchless AVX Checksum Synthesis
Calculating the mandatory 1's complement checksum for IP and TCP headers is traditionally a scalar loop bottleneck (`while (len > 1) { sum += *ptr++; len -= 2; }`).

The `tcp_wire_engine::compute_checksum` method replaces this with an auto-vectorizable block addition. 
- It aggregates the payload using 32-bit (or 64-bit) wide lanes.
- The compiler optimally lowers this to AVX2/AVX-512 vector instructions.
- The final 64-bit accumulator is folded into a 16-bit checksum using branchless shift-and-add arithmetic (`sum = (sum & 0xFFFF) + (sum >> 16)`).

## 4. Zero-Copy Integration
Because `raw_tcp_ipv4_frame` is a standard POD (Plain Old Data) type, it is never instantiated dynamically. The `tcp_gateway` simply casts (`reinterpret_cast`) the raw DMA ring pointers received from `matrix_nexus` directly to this struct. This allows O(1) protocol validation without copying a single byte from the network interface.