# SlabFlux Core: MPMC Sharded Conduit (`mpmc_sharded_conduit.hpp`)

## 1. Architectural Overview
Instead of utilizing a single highly-contested lock-free ring, the sharded conduit dynamically splits traffic across multiple parallel hardware lanes to drastically reduce atomic RFO (Read-For-Ownership) cache invalidation stalls.

## 2. Thread-Local Affinity
Threads dynamically select their lane using sticky routing based on physical topology (`sched_getcpu()`). This ensures operations remain strictly on locally-attached memory channels.

## 3. High-Throughput Matrix Routing
When scaling past 16+ logical cores, single-ring MPMC conduits inevitably bottle-neck on the central sequence ticket. The sharded conduit completely flattens this curve, achieving near-perfect horizontal scaling at the expense of strict total global ordering.