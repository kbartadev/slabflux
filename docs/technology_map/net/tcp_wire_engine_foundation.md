# Foundation: TCP Wire Engine & Encapsulation

## 1. Architectural Justification
Standard network stacks assemble packets by chaining discrete header structures (Ethernet, IP, TCP) via linked lists (`sk_buff` fragments). Transmitting this chain forces the NIC's DMA engine into expensive scatter-gather operations, and forces the CPU to execute multiple disparate memory writes.

The `tcp_wire_engine` relies on **Structural Fusion**. It defines the entire L2/L3/L4 encapsulation as a single contiguous, perfectly aligned C++ struct, ensuring that protocol synthesis is a unified hardware transaction.

## 2. Hardware Implementation Directives
- **64-Byte Cache Line Padding:** The combined size of Ethernet II (14), IPv4 (20), and TCP (20) is 54 bytes. The `raw_tcp_ipv4_frame` is explicitly padded with 10 bytes and marked `alignas(64)`. This guarantees the CPU writes the entire header block to the Write-Combining Buffer (WCB) in exactly one L1 cache line transaction.
- **AVX-Accelerated 1's Complement Checksum:** RFC 793 mandates a 16-bit 1's complement sum over the pseudo-header and payload. The engine avoids scalar `while` loops, auto-vectorizing the payload sum across 32-bit/64-bit wide SIMD lanes and folding the result branchlessly, maximizing arithmetic throughput.

## 3. Bibliography & Proofs
1. **Braden, R., Borman, D., & Partridge, C.** (1998). *Computing the Internet Checksum*. RFC 1071. (The mathematical foundation for efficiently computing byte-aligned 1's complement sums).
2. **Druschel, P., & Peterson, L. L.** (1993). *Fbufs: A high-bandwidth cross-domain transfer facility*. SOSP. (Early proofs that structurally contiguous network buffers drastically outperform chained memory fragments during hardware DMA).