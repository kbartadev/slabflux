# Blueprint: uring_duplex.hpp

## Architectural Overview
A unified bi-directional handler managing both network ingress and egress asynchronously across a single shared `io_uring` ring architecture.

## Core Logic & Mechanisms
- **CQE Interleaving**: Consumes completions for both sends and receives within the same clock iteration, maximizing data cache warmth.
- **Deadlock Immunity**: Employs strictly bounded batch limits to ensure an aggressive influx of data never starves outgoing transmission ACKs.