# SlabFlux Net: RTO & Congestion Matrix (`tcp_retransmit_timer.hpp`)

## 1. Architectural Overview
Traditional TCP stacks rely on interrupt-driven hardware timers to trigger Retransmission Timeouts (RTO) for unacknowledged packets. In a deterministic user-space stack, hardware interrupts disrupt the pipeline execution.

The `tcp_retransmit_timer` provides an integrated, tick-driven solution that evaluates RTO events sequentially during the `event_arbiter`'s Temporal Polling phase.

## 2. Integer-Only Jacobson/Karels Algorithm
To calculate the Retransmission Timeout, the engine must estimate the Round-Trip Time (RTT) and its variance. Standard implementations often use floating-point math or complex branching.

SlabFlux implements the RFC 6298 Jacobson/Karels algorithm using pure integer arithmetic and bitwise shifts:
```cpp
tracker.srtt += err / 8;
tracker.rttvar += (abs_err - tracker.rttvar) / 4;
tracker.rto = tracker.srtt + std::max(4u, 4 * tracker.rttvar);
```
This guarantees that evaluating network latency and updating congestion bounds executes in less than a dozen CPU cycles, keeping the Hot Path mathematically bounded.

## 3. Mathematical Window Aliasing (Congestion Control)
Standard TCP utilizes complex Congestion Control algorithms (CUBIC, Reno) that maintain a distinct Congestion Window (`cwnd`) to artificially throttle traffic.

SlabFlux simplifies this using **Mathematical Aliasing** directly mapped to hardware capability:
```cpp
tcb.rcv_wnd = (available_rx_capacity > 65535) ? 65535 : available_rx_capacity;
```
The Advertised Receive Window (`rcv_wnd`) sent to the remote peer is dynamically tied to the available capacity of the lock-free `rx_stream_ring`. 
- If the Sovereign Core's pipeline stalls, the ring fills up. 
- The `rcv_wnd` naturally shrinks to 0. 
- The remote peer initiates Zero-Window Probes.

This creates a perfect, backpressure-propagating feedback loop from the internal C++ ring buffer straight to the physical network wire without any artificial software thresholds.

## 4. Temporal Tick Integration
Instead of spawning a background thread to check for timeouts, the Sovereign Core manually drives time. The `on_temporal_tick` function is explicitly called with the current `__rdtsc()`-derived timestamp. If a TCB's unacknowledged frames exceed the calculated `rto`, the frames are deterministically re-queued for the `tcp_gateway`'s egress conduit.