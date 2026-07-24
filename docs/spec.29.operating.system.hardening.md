# Specification: Operating System Hardening & STS

This document defines the physical configuration and script specification required to achieve a "Sovereign" execution state. To guarantee sub-microsecond determinism, the OS must be relegated to a background role, strictly shielded from the RTE hot-path cores.

## 1. Kernel Boot Parameters (The Shield)
The following parameters must be passed to the kernel (GRUB) to isolate physical cores designated for the Ingress, Compute, and Journal nodes.

| Parameter | Requirement | Mechanical Purpose |
| :--- | :--- | :--- |
| `isolcpus` | Range (e.g., `1-15`) | Prevents the Linux scheduler from placing general tasks on hot cores. |
| `nohz_full` | Range (e.g., `1-15`) | Disables the kernel timer tick on isolated cores (Zero-Jitter mode). |
| `rcu_nocbs` | Range (e.g., `1-15`) | Offloads RCU callbacks to the housekeeping cores (Core 0). |
| `default_hugepagesz` | `2M` or `1G` | Sets the physical address resolution stride for SlabPools. |
| `intel_pstate` | `passive` | Allows the RTE to manually lock CPU frequency via STS. |

---

## 2. Tuning Script (STS) Spec
The STS is a root-level orchestration script that executes during the "Physical Boot" phase.

### A. CPU Governors & Frequency Locking
STS must force isolated cores into a fixed-frequency state.  Implementations must verify that the hardware actually accepted the frequency request and disable `turbo` boost to prevent thermal-induced jitter.

### B. IRQ Affinity Migration
STS must migrate hardware interrupts to Core 0. An industrial implementation must explicitly handle `irqbalance` and ensure MSI-X vectors for the primary NIC are correctly affinitized to the Ingress context.

### C. Intel CAT (Cache Allocation Technology)
STS utilizes the `pqos` utility. The specification requires validation that the CPUID actually supports L3/L2 CAT before attempting to enforce CLOS (Class of Service) partitions.
- **Expert Partition**: CLOS 1 (4 ways) - 0x00F.
- **Control Partition**: CLOS 2 (2 ways) - 0x030.

---

## 3. Binary Configuration Generator (`sf_cfg_gen`)
The `environment.hpp` component ingests configuration via a zero-copy `mmap` of `slabflux_config.bin`. The generator script must satisfy the following binary layout:

### Binary Layout (64-byte Aligned)
| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| 0x00 | `float` | `precision_delta` | Convergence epsilon. |
| 0x04 | `float` | `sanitizer_baseline` | Numerical floor. |
| 0x08 | `float` | `critical_drift` | Panic threshold for MSE. |
| 0x0C | `float` | `div_threshold` | Snapshot trigger epsilon. |
| 0x10 | `u8` | `drift_policy` | 0: Bit-Identical, 1: MSE, 2: PSNR. |
| 0x11 | `bool` | `weighted_san` | Toggle neighbor weighting. |
| 0x12 | `bool` | `drift_smooth` | Toggle EMA-based smoothing. |
| 0x13 | `u8[45]` | `padding` | Pad to 64 bytes (Cache Line). |

### Implementation Spec (Python)
```python
import struct

def generate_config(path, delta, baseline, critical, snapshot, policy):
    # Matches struct binary_config_payload in environment.hpp
    fmt = "ffffB??45x" 
    packed = struct.pack(fmt, delta, baseline, critical, snapshot, policy, True, True)
    with open(path, "wb") as f:
        f.write(packed)
```

---

## 4. Virtual Memory Hardening
To prevent the OS from interfering with `mpsc_hybrid_pool` allocations:
1. **THP Disable**: `echo never > /sys/kernel/mm/transparent_hugepage/enabled`. SLABFLUX manages HugePages manually via `MAP_HUGETLB`.
2. **Swap Disengagement**: `swapoff -a`. Eliminates the risk of anonymous slab pages being moved to non-volatile storage.
3. **Dirty Ratio**: STS must set `vm.dirty_ratio = 10` and `vm.dirty_background_ratio = 5` to ensure `durable_journal` writes are flushed to the NVMe controller with minimal kernel buffering.

---

## 5. Orchestration Script Catalog (`/scripts/`)
The following industrial-grade utilities provide granular control over the silicon and kernel state. They are designed for idempotency and strict capability-aware execution.

| Script | Mechanical Purpose | Requirement |
| :--- | :--- | :--- |
| **[bandwidth_enforcer.sh](../scripts/bandwidth_enforcer.sh)** | Intel MBA (Memory Bandwidth Allocation) | Prevents noisy neighbors from saturating the memory controller. |
| **[cache_partitioner.sh](../scripts/cache_partitioner.sh)** | Intel CAT (Cache Allocation Technology) | Hard-pins Experts and Conduits into dedicated L3 cache ways. |
| **[shield_cores.sh](../scripts/shield_cores.sh)** | CPU Isolation (cgroups/cpusets) | Physically moves all non-RTE tasks to housekeeping cores. |
| **[jitter_shield.sh](../scripts/jitter_shield.sh)** | OS Noise Suppression | Disables watchdogs, thermal throttling, and forces TSC clocksource. |
| **[nic_flow_director.sh](../scripts/nic_flow_director.sh)** | Hardware Receive Steering | Maps specific NIC RX queues to the Ingress Core's local L1 cache. |
| **[silicon_priority.sh](../scripts/silicon_priority.sh)** | Uncore/Ring-bus Pinning | Locks the CPU internal interconnect priority to maximum. |
| **[interrupt_lock.sh](../scripts/interrupt_lock.sh)** | Hardware IRQ Pinning | Affinitizes NIC and NVMe MSI-X vectors to dedicated I/O threads. |
| **[pcie_latency_shield.sh](../scripts/pcie_latency_shield.sh)** | Bus Bandwidth Tuning | Disables PCIe ASPM and maximizes Max Read Request Size (MRRS). |
| **[rcu_isolation.sh](../scripts/rcu_isolation.sh)** | RCU Callback Migration | Ensures kernel RCU maintenance never executes on isolated cores. |
| **[tdx_seal.sh](../scripts/tdx_seal.sh)** | Intel TDX Memory Encryption | Enables hardware-level isolation for sensitive state blocks. |
| **[vfio_shield.sh](../scripts/vfio_shield.sh)** | I/O Virtualization Isolation | Protects SR-IOV virtual functions used by kernel-bypass drivers. |
| **[init.sh](../scripts/init.sh)** | Master Physical Bootstrapper | Validates environment constraints before launching the RTE. |
| **[sf_cfg_gen.py](../scripts/sf_cfg_gen.py)** | Binary Config Generator | Generates 64-byte aligned manifest for zero-jitter updates. |

## References
- **Intel Corporation** - *Cache Allocation Technology (CAT) Guide*.
- **Linux Kernel** - *Real-Time Systems (PREEMPT_RT) Documentation*.
- **PCI-SIG** - *PCI Express Base Specification (ASPM & Latency Optimization)*.
