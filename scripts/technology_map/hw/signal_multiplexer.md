# SlabFlux Conduit: Signal Multiplexer (`signal_multiplexer.hpp`)

## 1. Architectural Overview
In deterministic networking, an inbound market signal often needs to be distributed to multiple isolated execution lanes simultaneously (e.g., sending a trade tick to the Risk Engine, the AI Cognitive Synapse, and the Order Router). 
The `signal_multiplexer` is a hardware-level broadcast node that physically duplicates pointer references into multiple downstream `spsc_conduit` structures without dynamic iteration.

## 2. Compile-Time Loop Unrolling
Standard multiplexers loop over an array of subscriber interfaces. At nanosecond latencies, the branch evaluation of the loop condition (`i < size`) causes unacceptable jitter.

The `signal_multiplexer` leverages C++17 Fold Expressions:
- The downstream conduits are passed to the multiplexer as a `std::tuple` at compile-time.
- The `broadcast()` method unpacks the tuple and executes a flat, unrolled sequence of `conduit.try_push(payload)` calls.
- The C++ compiler optimizes this into a continuous block of branchless `MOV` and `CAS` instructions.

## 3. Backpressure Arbitration
If one downstream queue saturates (e.g., the logging thread stalls), a traditional broadcaster would block the hot path. 
The `signal_multiplexer` utilizes non-blocking semantics:
- It evaluates backpressure individually. 
- If the `AuditBus` is full, it drops the payload for that specific target and logs a telemetry fault, but instantly proceeds to broadcast to the critical `ExecutionBus` unimpeded.
- This prevents a stalled background thread from locking up the primary deterministic trading matrix.