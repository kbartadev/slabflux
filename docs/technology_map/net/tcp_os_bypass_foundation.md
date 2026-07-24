# Foundation: User-Space TCP/IP & OS-Bypass Sockets

## 1. Architectural Justification
Standard operating system TCP/IP stacks (Linux, BSD) are designed for general-purpose fairness, relying heavily on hardware interrupts, spinlocks, and complex `sk_buff` allocations. When deploying high-frequency trading (HFT) applications over Kernel-Bypass interfaces (like DPDK or AF_XDP), falling back to the OS for TCP stream reassembly entirely negates the latency benefits of DMA polling.

The SlabFlux User-Space TCP/IP subsystem replaces the OS stack with a strictly deterministic, bounded, and wait-free execution manifold. It guarantees that TCP state transitions, congestion control, and stream fragmentation execute completely within the L1/L2 cache footprint of the Sovereign Core, eliminating context switches and Ring 0 transitions.

## 2. Hardware Implementation Directives
- **Invariant-Driven State Machines:** Textbook TCP stacks utilize massive, deeply nested `switch(state)` blocks that thrash the CPU branch predictor. SlabFlux flattens the TCP lifecycle into an 8-bit `phase_mask`, executing state transitions (e.g., `SYN_RCVD` $\to$ `ESTABLISHED`) via $O(1)$ bitwise operations.
- **Structural Header Fusion:** Rather than chaining packet headers via linked lists, SlabFlux dictates a rigid 64-byte memory geometry (`raw_tcp_ipv4_frame`). This ensures that generating outbound Ethernet, IPv4, and TCP headers requires exactly one L1 cache-line transaction.
- **Mathematical Window Aliasing:** Instead of running isolated TCP congestion algorithms (like CUBIC) with separate buffers, the TCP Advertised Window (`rwnd`) is dynamically mapped to the physical available capacity of the internal Wait-Free conduits (`spsc_ring_conduit`). If the pipeline stalls, the network window naturally closes, creating a perfect zero-allocation backpressure loop.
- **Tick-Driven RTO:** Operating system timer interrupts (`timerfd`) induce microsecond jitter. SlabFlux integrates Jacobson/Karels RTT/RTO math directly into the Sovereign Core's existing Temporal Tick, evaluating retransmissions sequentially on the hot path without floating-point arithmetic.

## 3. Bibliography & Proofs
1. **Peter, S., Li, J., Zhang, I., Ports, D. R., Woos, D., Krishnamurthy, A., ... & Anderson, T.** (2014). *Arrakis: The operating system is the control plane*. OSDI. (Proves that removing the OS from the data plane and managing TCP directly in user space yields massive latency reductions).
2. **Honda, N., Huici, F., Raiciu, C., & Handley, M.** (2014). *mTCP: a Highly Scalable User-level TCP Stack for Multicore Systems*. USENIX NSDI. (Architectural proofs on lock contention in Linux VFS/sockets and the necessity of user-space TCP flow control).
3. **Postel, J.** (1981). *Transmission Control Protocol*. RFC 793. (The foundational specification of TCP semantics, which SlabFlux respects on the wire while replacing its internal implementation).
4. **Paxson, V., & Allman, M.** (2000). *Computing TCP's Retransmission Timer*. RFC 6298. (The mathematical foundation for the integer-only RTO/RTT variance tracking implemented in SlabFlux).