# SlabFlux Net: Egress Shield Wiring (`slabflux/net/egress_shield_wiring.hpp`)

## 1. Architectural Justification
Before deterministic state objects are flushed to the physical wire, they must be converted back into contiguous byte spans. The egress shield reverses the bimodal input projection with zero allocations.

## 2. Hardware Implementation Directives
- **Bit-Algorithmic Shuffling**: Utilizes AVX-512 BITALG instructions (`_mm512_popcnt_epi16`, `_mm512_shuffle_epi8`) to flatten sparse C++ structures into dense wire layouts, minimizing PCIe transmission overhead.
- **Outbound Resonance Tagging**: Injects the reverse conjugate matrix into the payload's tail, ensuring downstream consumer nodes can execute their own bimodal shield validations.

## 3. Pipeline Integration
Sits at the end of the deterministic execution DAG. Consumes the output from the `branchless_engine`, flattens it, and passes the resulting memory span to `uring_egress` or `dpdk_egress` for physical transmission.