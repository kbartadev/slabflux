# Blueprint: mirrored_journal.hpp

## Architectural Overview
Orchestrates High-Availability storage patterns entirely in user-space, dispatching bit-perfect copies of state across multiple distinct NVMe/SSD physical paths concurrently.

## Core Logic & Mechanisms
- **Parallel Submission Rings**: Invokes multiple isolated `io_uring_durable_journal` backend endpoints transparently beneath a unified writing interface.
- **Amortized Reaping**: Evaluates independent storage completion queues fairly during `poll_completions()`, preventing slower secondary disks from creating head-of-line blocking for primary fast disk writes.