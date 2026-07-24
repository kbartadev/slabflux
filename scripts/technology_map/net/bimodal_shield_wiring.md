# SlabFlux Net: Bimodal Shield Wiring (`bimodal_shield_wiring.hpp`)

## 1. Architectural Overview
The `bimodal_shield_wiring` acts as the secure Demilitarized Zone (DMZ) between the I/O Threads (NIC handling) and the Deterministic Compute Thread. It is responsible for tearing down the external envelope and sealing the data inside the internal envelope.

## 2. Ingress Handoff (The Bimodal Shift)
1. **Tier 1 (Outer Perimeter)**: The raw PCIe buffer is cast directly to a `sovereign_signal`. It executes Symplectic Resonance Fencing (SRF). If the 1-cycle AVX-512 VNNI tension fractures, the data is dropped to the allocator immediately.
2. **Tier 2 (Inner Perimeter)**: If valid, the wiring extracts the raw payload and physically wraps it inside an `autologous_isomorphism` (ACI Envelope).

## 3. Temporal Entanglement
Before pushing the ACI envelope into the lock-free Compute conduit, the wiring invokes `embed_symmetry()`, entangling the memory pointer with the incoming Logical Sequence Number (LSN).
This ensures that by the time the Compute Engine pops the data off the queue, the hardware can mathematically prove the pointer hasn't suffered a Use-After-Free or cache-degradation event in transit.