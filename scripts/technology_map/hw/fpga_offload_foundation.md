# Foundation: FPGA SmartNIC Offload (`slabflux/hw/fpga_offload.hpp`)

## 1. Architectural Justification
In the sub-microsecond latency arms race, even optimized AVX-512 operations consume critical nanoseconds. The `fpga_offload` module provides the API boundaries to physically evict computational tasks from the CPU entirely, executing them on programmable silicon (SmartNICs) closer to the network transceiver.

## 2. Hardware Implementation Directives
- **Direct Register Interfacing**: Maps directly to the PCIe Base Address Registers (BARs) exposed by the FPGA hardware. The CPU writes micro-commands directly into the FPGA's MMIO space, delegating full packetization and TCP sequence generation to the NIC.
- **Line-Rate Checksumming**: Offloads Ethernet FCS and UDP/TCP payload CRC32 checksums to the FPGA, saving the CPU from running `_mm_crc32_u64`.
- **Market Data Prefiltering**: Instantiates hardware-level routing rules on the FPGA to instantly drop irrelevant market data symbols before they cross the PCIe bus into host memory, preserving L3 cache and memory bandwidth.
- **SFINAE Fallback**: Transparently falls back to optimized software equivalents (AVX-512) if deployed on non-accelerated silicon.

## 3. Bibliography & Proofs
1. **Caulfield, A. M., et al.** (2016). *A cloud-scale acceleration architecture*. IEEE/ACM International Symposium on Microarchitecture (MICRO). (SmartNIC and FPGA integration).
2. **Firestone, D., et al.** (2018). *Azure Accelerated Networking: SmartNICs in the Public Cloud*. NSDI.
3. **Varghese, G.** (2004). *Network Algorithmics*. (Hardware vs. Software bottleneck analysis).