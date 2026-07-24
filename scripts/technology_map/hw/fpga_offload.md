# SlabFlux HW: FPGA SmartNIC Offload (`fpga_offload.hpp`)

## 1. Architectural Overview
In the sub-microsecond latency arms race, even AVX-512 operations take too long. The `fpga_offload` module provides the API boundaries to physically evict computational tasks from the CPU and execute them on programmable silicon (SmartNICs) directly on the network card.

## 2. Direct Register Interfacing
The module maps directly to the PCIe Base Address Registers (BARs) exposed by the FPGA hardware.
- Instead of constructing software payloads and pushing them through `af_xdp`, the CPU writes tiny command structs directly into the FPGA's MMIO (Memory-Mapped I/O) space.
- The FPGA assumes immediate responsibility for network packetization, TCP sequence numbering, and transmission.

## 3. Offloaded Primitives
The API specifically targets mathematically intense, highly parallelizable operations:
- **Line-Rate CRC32 Generation**: The FPGA computes the Ethernet FCS and payload CRC32 checksums as the bits fly out of the transceiver, saving the CPU from running `_mm_crc32_u64`.
- **Hardware Sequencing (LSN)**: The Logical Sequence Number generator is pushed to the NIC. The FPGA stamps outgoing UDP Multicast packets with sequential IDs at the exact moment of transmission, eliminating OS scheduling jitter from the timeline.
- **Market Data Filtering**: The FPGA can be configured with routing rules to instantly drop irrelevant market data symbols before they ever cross the PCIe bus into host memory, preserving L3 cache and memory bandwidth.

## 4. Seamless Fallback
If the node is deployed in a cloud environment without SmartNIC acceleration, the `fpga_offload` template structure utilizes SFINAE and `isa_guard` checks to transparently fall back to highly optimized software equivalents (`avx512_search_backend`, `af_xdp_ingress`) without altering the upper-level business logic.