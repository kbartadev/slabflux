# Deployment and Kernel Tuning

## Thread Affinity

For O(1) latency guarantees, threads must be pinned to isolated CPU cores.

- Use `taskset` or `pthread_setaffinity_np`.
- Isolate cores at the boot level via `isolcpus` in GRUB.

## NUMA Alignment

The `runtime_domain` and its associated worker threads must reside on the same NUMA node to avoid cross-socket memory latency.

## Optimization Flags

`-O3 -march=native -flto` are mandatory.

Link-Time Optimization (LTO) is critical for inlining the C++20 Concept-based dispatch chains. Skipping this violates performance integrity.
