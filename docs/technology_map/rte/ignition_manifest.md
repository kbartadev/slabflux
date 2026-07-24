# SlabFlux RTE: Ignition Manifest (`ignition_manifest.hpp`)

## 1. Architectural Overview
Booting a bare-metal deterministic execution engine is significantly more complex than calling `main()`. The CPU caches must be partitioned, memory pinned, hardware capabilities validated, and non-temporal structures zeroed. The `ignition_manifest` orchestrates this highly sensitive boot sequence.

## 2. Boot Sequencer
The manifest enforces a strict topological ordering for initialization:
1. **Silicon Audit**: Runs `isa_guard` to verify AVX-512, WAITPKG, and hardware RDTSC capabilities.
2. **Memory Sovereignty**: Allocates the `hugepage_allocator` pools and locks them to physical RAM (`mlockall`).
3. **Thread Pinning**: Invokes the `hardware_topology` to bind the core threads to their exact, isolated CPU cores.
4. **Cache & Power Tuning**: Engages the `power_governor` (disabling C-states) and `cache_partitioner` (Intel CAT isolation).
5. **TLB Pre-Faulting**: Runs the `tlb_warmup` to seat the physical memory tables in hardware.

## 3. Cryptographic State Validation
Before opening the `demux_gateway` to live network traffic, the manifest coordinates with the `tpm_attestor`. It mathematically validates the integrity of the binary and the active configuration schema.

## 4. Atomic Go-Live Transition
Once all subsystems report "Green", the manifest executes a final `std::atomic_thread_fence(std::memory_order_seq_cst)`. It switches the global `node_runtime` state to `ACTIVE`, simultaneously unmuting the NIC ingress polling and the `baremetal_egress` rings.