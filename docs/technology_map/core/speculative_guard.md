# SlabFlux Core: Speculative Guard (`speculative_guard.hpp`)

## 1. Architectural Overview
Modern superscalar processors rely heavily on Branch Prediction and Speculative Execution to maintain performance. However, these features introduced severe vulnerabilities (Spectre, Meltdown). Enabling full OS-level mitigations (PTI, Retpolines) destroys deterministic throughput. The `speculative_guard` provides targeted, zero-overhead manual mitigations.

## 2. Architectural Fencing (`lfence`)
Instead of disabling speculative execution globally, the guard injects explicit hardware fences exactly where necessary:
- Whenever the system reads an index from an untrusted source (e.g., extracting an array offset from an incoming TCP payload), it is vulnerable to bounds-check bypass attacks.
- The guard injects `_mm_lfence()` immediately after the bounds validation.
- This instruction physically forces the CPU to wait until the bounds-check resolves before continuing execution, mathematically preventing the processor from speculatively pulling unauthorized memory into the L1 cache.

## 3. Branchless Design Synergy
Because the core `branchless_engine` and `pipeline` dispatch matrices are almost entirely devoid of conditional `if` statements (relying instead on CMOV and SFINAE unrolling), the attack surface for branch target injection is naturally mitigated. The `speculative_guard` is therefore deployed surgically only at the outermost network-to-compute boundaries (`demux_gateway`), preserving maximum bandwidth.