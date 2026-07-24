# Blueprint: baremetal_egress.hpp

## Architectural Overview
Extreme lowest-latency transmission layer. Bypasses all drivers and frameworks to execute `mmap` direct writes into PCIe MMIO doorbell registers using hardware store fencing (`_mm_sfence`).