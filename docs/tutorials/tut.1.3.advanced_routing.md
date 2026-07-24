# Tutorial 1.3: Advanced Routing Matrices

When scaling beyond Single-Producer Single-Consumer (SPSC) pipelines, standard mutexes and `fetch_add` queues induce severe MESI protocol invalidation storms (RFO stalls) across the interconnect bus. SlabFlux provides specialized matrices to fan-out and fan-in data seamlessly.

## 1. Round-Robin Switch (`bridge/round_robin_switch.hpp`)
The `round_robin_switch` is a highly optimized Fan-Out routing node used to shard high-frequency event streams symmetrically across downstream worker threads.

### O(1) Lock-Free Sharding
It abandons shared states entirely:
- It utilizes an isolated, cache-aligned `next_` cursor to prevent RFO stalls.
- Instead of modulo math, it uses branchless conditional subtraction: `next_ = (idx + 1 == NumOutputs) ? 0 : idx + 1;`.
- It accepts statically bound output conduits via the `bind_track` configuration method.

### Backpressure Absorption
If a target conduit is congested, the conduit's internal `try_push` fails. The switch *does not spin-wait*. It instantly advances the cursor and attempts to route the event to the next available worker in the same CPU cycle via the `route()` method.

## 2. Cross-Orthogonal Queue (`cross_orthogonal_queue.hpp`)
The `cross_orthogonal_queue` is a specialized MPMC (Multi-Producer Multi-Consumer) structure that applies orthogonal manifold principles to prevent hotspot contention.

### Spatial Dispersion (Anti-Thundering Herd)
In standard queues, 32 threads starting simultaneously will all contend for index `0`. 
In the Cross-Orthogonal Queue, threads do not start scanning at `(0,0)`.
- Entry vectors are calculated using a Knuth Multiplicative Hash of the hardware `std::thread::id`.
- This guarantees that memory accesses physically scatter across distinct cache banks and NUMA nodes.

### Spinless Progression
If a `compare_exchange_strong` fails (meaning another thread claimed the cell), the thread *does not loop*. It acknowledges the cell is occupied and jumps to the next cell in $O(1)$ time. This yields a guaranteed Wait-Free bound on producer operations.

## 3. Hands-On: Wait-Free Ingress Sharding

```cpp
#include "slabflux/bridge/round_robin_switch.hpp"
#include "slabflux/core/mpmc_conduit.hpp"

int main() {
    constexpr int NUM_WORKERS = 4;
    
    // Downstream conduits for the worker threads
    slabflux::core::mpmc_conduit<int*, 1024> workers[NUM_WORKERS];
    
    // Instantiate the switch
    slabflux::bridge::round_robin_switch<int, NUM_WORKERS> sharder;
    
    // Statically bind the output tracks
    for (std::size_t i = 0; i < NUM_WORKERS; ++i) {
        sharder.bind_track(i, workers[i]);
    }
    
    for (int i = 0; i < 1'000'000; ++i) {
        int* payload = new int(i); // Note: Use pools in production!
        
        // Distributes the pointer to the next available worker conduit
        while (!sharder.route(payload)) {
            asm volatile("pause" ::: "memory"); // Yield if all targets are full
        }
    }
    
    return 0;
}
```
*Note: The usage of `new` in the hands-on code is strictly for illustrative brevity. In production, pointers must be acquired from `spsc_pool` or `mpmc_pool`.*