# Advanced Routing & Dispatch

Beyond the linear flow of the `slabflux::pipeline` and the foundational SPSC `conduit`, the `slabflux::bridge` and `slabflux::compute` namespaces expose advanced topological routing matrices for sharding and massive workload distribution.

## `slabflux::bridge::round_robin_switch`
A heavily contested, lock-free load balancer designed for extreme-throughput event distribution.
* **O(1) Sharding:** Symmetrically distributes incoming event torrents across multiple downstream `conduit` instances. It relies entirely on a wait-free, atomic round-robin algorithm, eliminating mutex bottlenecks.
* **Fan-Out Architecture:** The premier component for horizontally scaling stateless processing engines (e.g., evenly fanning out parsed HTTP request objects to an isolated pool of worker threads).

## `slabflux::compute::hierarchical_dispatch_chain` & `mixed_dispatch_matrix`
Advanced, non-linear routing tables resolved entirely during compilation.
* **Vtable-less Polymorphism:** Facilitates highly complex, conditional routing topologies (e.g., dispatching events derived from runtime `TYPE_ID`s or embedded bit-flags) without suffering the severe latency penalties of `dynamic_cast` or virtual function pointer chasing.
* **Aggressive Inline Evaluation:** The compiler is forced to aggressively inline the entire routing logic tree. This transmutes complex dispatch matrices into flattened, optimally branch-predicted assembly instructions perfectly suited for the L1 cache.
