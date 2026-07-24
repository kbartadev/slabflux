# Blueprint: af_xdp_ingress.hpp

## Architectural Overview
The `af_xdp_ingress` module represents the ultimate low-latency network reception layer for Linux platforms. It completely bypasses the monolithic Linux kernel network stack, allowing the SlabFlux execution core to read raw Ethernet frames directly out of the Network Interface Card (NIC) receive rings using UMEM mapping and SIMD hardware-aligned prefetching.