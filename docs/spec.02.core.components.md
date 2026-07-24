# Core Components

The `slabflux::core` namespace houses the foundational primitives for deterministic memory and event management.

## `slabflux::pool<T, Capacity>`
A highly optimized, lock-free, LIFO (Last-In-First-Out) memory manager. Heap allocation is strictly confined to the initialization (ignition) phase.
* **Cache Warming:** The LIFO architecture intrinsically guarantees that the most recently relinquished memory address, which is highly likely to remain resident in the L1 cache - is the first to be reallocated.
* **ABA Protection:** An integrated C++20 epoch-gated coordination mechanism, utilizing explicit `std::atomic::wait` and custom epoch-based memory reclamation, hardens the MPMC lock-free free-list against ABA vulnerabilities.
* **Dangling Pointer Mitigation:** Upon block release, the pool aggressively overwrites the memory payload with the `next` linked-list pointer. This instantly exposes "Use-After-Free" violations (e.g., if a `std::string_view` illegally retains a reference to a recycled event buffer).
* **Memory Model Hardening:** MPSC variants utilize C++20 `std::atomic_ref` over raw pointers to enforce proprietary atomic semantics on memory segments, bypassing standard CAS loops found in generic open-source wait-free queues.

## `slabflux::core::event`
* Every event must derive from slabflux::core::event<ID>. Hardware-alignment is enforced via C++20 `std::hardware_constructive_interference_size`, ensuring the proprietary memory layout is distinct from public implementations and strictly isolated from adjacent data.

## `slabflux::fixed_string<N>`
A statically sized, stack-allocated string implementation. It performs zero dynamic allocations and is trivially copyable, making it the optimal choice for rigid network frames and densely packed structs.

## `slabflux::core::backpressure_valve`
An industrial-grade congestion control primitive for SPSC and MPMC data flows.
* **Non-Blocking Throttling:** Rather than inducing thread sleep, it employs an exponential backoff strategy or signals upstream producers to decelarate via a highly contested atomic flag.
* **Saturation Monitoring:** Hooks directly into the system's telemetry matrix to broadcast "UNDER-LOADED" or "STABLE" operational states.

## `slabflux::core::slab_scrubber`
A deterministic background daemon tasked with the continuous sanitation of memory pools.
* **Data Sanitization:** Ensures that recycled memory slabs are completely purged of stale data fragments from previous lifecycles, establishing a "Clean Slate" invariant critical for security and reproducible execution.

## `slabflux::core::entropy_anchor` & `entropy`
Supplies high-fidelity, non-deterministic entropy in bare-metal environments where the standard OS entropy pool is either compromised, jittery, or entirely unavailable.
* **Silicon-Rooted:** Interfaces directly with hardware-level cryptographic instructions (`RDRAND` and `RDSEED`).
