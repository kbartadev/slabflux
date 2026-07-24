# SlabFlux Core: Sovereign Core (`sovereign_core.hpp`, `node_runtime.hpp`)

## 1. Architectural Overview
The `sovereign_core` is the grand orchestrator of the entire SlabFlux execution environment. It acts as the immutable `main()` wrapper, replacing standard application lifecycles with a strictly verified, hardware-enforced boot sequence that locks the application into bare-metal sovereignty.

## 2. The Ignition Lifecycle
When the binary launches, the `sovereign_core` prevents any dynamic logic from running until the `ignition_manifest` is completely satisfied:
1. **Memory Locking**: Claims and locks all HugePages and `eternal_memory` arrays to physical RAM.
2. **OS Sealing**: Executes `binary_seal` to lock the executable, and evaluates `isa_guard` to verify AVX-512 capabilities.
3. **Thread Instantiation**: Spawns the heavily isolated `stall_free_nexus` (Compute), `sovereign_egress` (Network TX), and `io_uring_ingress` (Network RX) threads.
4. **Silicon Binding**: Executes `sys::topology_enforcer` to permanently pin these threads to their designated, cache-isolated CPU cores.

## 3. The Stall-Free Nexus
Once the cluster validates its attestation (via `tpm_attestor`), the `sovereign_core` enters the infinite event loop.
- It invokes the `round_robin_poller` and the `event_arbiter`.
- From this point forward, the core thread never executes a system call, never attempts to acquire a mutex, and never yields to the OS.
- It becomes a physically dedicated extension of the C++ logic matrix, processing the global state until the process is violently terminated or a `SLAB_HARDWARE_HALT` occurs.