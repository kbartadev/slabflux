# Blueprint: Core Routing & Gateways Architecture

## Architectural Overview
Traffic distribution inside the engine avoids locks, queues, and expensive context switches. It utilizes mathematical indexing and compile-time evaluations to multiplex inputs and distribute workloads evenly.

## Core Components
- **Fan-Out & Fan-In (`round_robin_switch.hpp`, `round_robin_poller.hpp`)**: Replaces complex thread scheduling with O(1) branchless mathematical rotation arrays. This mechanism guarantees fair distribution across worker threads while preserving the CPU branch target buffer.
- **Type Demultiplexing (`demuxer.hpp`)**: Utilizes compile-time C++20 Fold Expressions to evaluate 64-bit `tagged_pointer` envelopes. It matches integer IDs to their corresponding handlers, executing jump logic entirely at compilation phase.
- **Sequence Reconstruction (`hole_puncher.hpp`)**: Implements a jitter-tolerant reorder buffer. It holds misaligned network packets in memory, emitting them only when strict LSN continuity is met, gracefully absorbing network entropy.
- **Network Gateways (`event_gateway.hpp`, `network_conduit.hpp`)**: The absolute boundary of the engine. It strips transport headers and directly casts raw byte payloads into strictly-aligned C++ structural elements, performing zero-copy ingress.
- **Hotpatching (`hotpatch_bridge.hpp`)**: Safely pivots logic pointers utilizing acquire-release memory semantics, enabling real-time logic rotation without interrupting the continuous flow of events.