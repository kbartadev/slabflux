# Benchmarking Methodology

To validate performance integrity, SLABFLUX mandates strict benchmarking protocols for any changes to the pool or conduit.

## 1. Environment Requirements

- **CPU:** Intel Xeon or AMD EPYC. Disable Hyper-Threading / SMT.
- **OS:** Linux Kernel 5.15+
- **Isolation:** Use `isolcpus` in GRUB to isolate target cores from the OS scheduler.

## 2. Measurement Tools

SLABFLUX uses Google Benchmark to measure mean throughput and HDR Histogram to measure tail latency (P99, P99.9, P99.99).
Average execution times are irrelevant in HFT; the maximum latency outlier dictates system reliability.

## 3. False Sharing Validation

Always ensure that `std::hardware_destructive_interference_size` matches the L1 cache line size of the physical CPU executing the tests.

You can verify cache misses during the benchmark using `perf`:
~~~bash
perf stat -e cache-misses,cache-references ./benchmarks/bench_conduit
~~~

A successful lock-free architecture will show near-zero L1 cache misses during a continuous SPSC flow.
