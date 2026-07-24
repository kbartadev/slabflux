# SlabFlux I/O: AF_XDP Ingress (`slabflux/io/af_xdp_ingress.hpp`)

## 1. Architectural Overview & Justification
The `af_xdp_ingress` module represents the ultimate low-latency network reception layer for Linux platforms. It completely bypasses the monolithic Linux kernel network stack (Netfilter, iptables, `sk_buff` allocation, and protocol demuxing), allowing the SlabFlux execution core to read raw Ethernet frames directly out of the Network Interface Card (NIC) receive rings.

## 2. Zero-Copy Kernel Bypass Architecture

### UMEM Memory Mapping
The module maps a continuous block of physical memory (the UMEM) between user-space and the NIC hardware. 
1. The NIC's DMA engine writes incoming Ethernet frames directly into the UMEM.
2. The kernel's XDP (eXpress Data Path) program instantly redirects the frame to the `AF_XDP` socket.
3. The SlabFlux thread, spinning on the user-space RX Ring, sees the packet in single-digit microseconds without any `read()` or `recvfrom()` system calls.

### Hardware-Aligned Pre-fetching
Since the physical location of the next packet is known in advance via the `AF_XDP` ring descriptors, the ingress loop aggressively utilizes `_MM_HINT_T0` software prefetching. The CPU pre-loads the Ethernet header into the L1 cache before the loop actually parses it, masking DRAM latency completely.

## 3. In-Line Protocol Dissection & Directives
To maximize cache locality, the packet is parsed immediately upon discovery within the ring buffer:
- **SIMD Header Parsing**: Rapid protocol dissection (MAC, IPv4, UDP) is handled by AVX2/AVX-512 aligned scanners.
- **Direct Memory Casting**: Because the payload sits in pinned UMEM, SlabFlux uses zero-copy pointer casting to map `sovereign_signal` or `wire_frame_lsn` structures directly over the raw byte stream. 

## 4. Core Integration & Handoff
Once validated, the raw packet pointer is immediately routed to the deterministic core.
- **Wait-Free Handoff**: The packet pointer is pushed into a `spsc_ring_conduit`, crossing the boundary from the I/O thread to the Compute thread.
- **Zero Allocation**: The packet data is never copied. The compute engine processes the data directly from the UMEM segment. Once the compute engine finishes, the pointer is recycled back into the AF_XDP Fill Ring to receive a future packet.

## 5. Security & Isolation
Because `AF_XDP` bypasses `iptables`, the `af_xdp_ingress` incorporates a hardware-level `sovereign_judge`. Invalid IP addresses, malformed CRCs, or unrecognized port traffic are instantly dropped at the edge, recycling the UMEM block before it ever touches the execution pipeline.