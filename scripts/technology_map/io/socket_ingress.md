# Blueprint: socket_ingress.hpp

## Architectural Overview
Provides the definitive syscall-bound baseline for network ingestion on systems lacking advanced kernel-bypass integrations.

## Core Logic & Mechanisms
- **Polling Evaluator**: Executes direct POSIX `recv` system calls to pull data into application memory.
- **Copy Fallback**: Copies payload out of `sk_buff` structs mechanically, establishing the base metrics used to compare the framework's zero-copy accelerations.