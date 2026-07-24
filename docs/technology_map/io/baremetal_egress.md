# SlabFlux I/O: Baremetal Egress (`slabflux/io/baremetal_egress.hpp`)

## 1. Architectural Justification
When absolute hardware limits must be reached, even standard PMDs (like DPDK) introduce minimal framework overhead. The `baremetal_egress` bypasses all intermediary abstractions to write descriptors directly into PCIe Memory-Mapped I/O (MMIO) registers.

## 2. Hardware Implementation Directives
- **Direct PCIe Doorbells**: Uses AVX instructions to write network descriptor payloads directly to the NIC's physical PCI doorbell registers over the motherboard bus.
- **Zero-Framework Mapping**: Extracts physical base addresses from `sysfs` (`/sys/bus/pci/.../resource`) and invokes `mmap` to acquire raw register access without external library dependencies.
- **Silicon Fencing**: Applies strict `_mm_sfence` before ringing the doorbell to prevent PCI-Express transaction reordering.

## 3. Deterministic Pipeline Integration
Like other egress modules, it consumes validated outbound `wire_frame` pointers directly from the wait-free `spsc_conduit`. However, instead of passing these to a driver loop or kernel queue, it translates the frame addresses instantly into PCIe doorbell rings, ensuring the deterministic core's output hits the physical wire with absolute zero software overhead.