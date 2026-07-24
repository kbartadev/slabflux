# Foundation: TCP Retransmit Timer & Congestion Matrix

## 1. Architectural Justification
Traditional TCP stacks rely on hardware interrupts or OS-level timer threads (`epoll_wait` with timeouts) to trigger Retransmission Timeouts (RTO). In a high-frequency Sovereign Core, yielding to the OS scheduler or processing asynchronous interrupts causes catastrophic latency jitter.

The `tcp_retransmit_timer` natively binds TCP temporal progression to the SlabFlux `event_arbiter`. RTO evaluation is executed synchronously on the hot path via the deterministic Temporal Tick, relying on pure integer math without floating-point transitions.

## 2. Hardware Implementation Directives
- **Integer-Only Jacobson/Karels:** Estimating Round-Trip Time (RTT) variance is executed purely with bitwise shifts and unsigned integer accumulation (`err / 8`). This isolates the ALU from FPU context-switching overheads.
- **Mathematical Window Aliasing (Zero-Allocation Congestion):** Traditional congestion control dynamically scales a software `cwnd` limit. SlabFlux aliases the Advertised Receive Window (`rcv_wnd`) directly to the available hardware capacity of the internal Wait-Free `spsc_ring_conduit`. If the conduit stalls, the window organically closes, generating zero-window backpressure exactly mapped to physical memory bounds.

## 3. Bibliography & Proofs
1. **Paxson, V., & Allman, M.** (2000). *Computing TCP's Retransmission Timer*. RFC 6298. (The authoritative, integer-optimized algorithm for calculating network variance without floating point math).
2. **Jacobson, V.** (1988). *Congestion avoidance and control*. ACM SIGCOMM. (The seminal paper establishing how TCP sliding windows can organically map to internal router/memory capacities).