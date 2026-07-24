# Blueprint: System ABI

## Architectural Overview
Baremetal system ABI wrappers. Bypasses standard libc syscalls for direct register-bound kernel interaction (e.g., inline assembly for `write` or `mmap`).