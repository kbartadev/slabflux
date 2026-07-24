# Tutorial 6.2: Out-of-Order Quarantine

## 1. The UDP Multicast Reordering Problem
In a distributed Causal Mesh, events (such as market data updates) are typically broadcast via UDP multicast to minimize latency. UDP does not guarantee packet ordering. If your node receives Logical Sequence Number (LSN) `1`, then `3`, then `4`, you cannot process `3` and `4` because `2` is missing. 

Standard reorder buffers use `std::map` or priority queues (`O(log N)` insertion), which introduce unacceptable runtime branching and heap allocations.

## 2. The SlabFlux `hole_puncher.hpp`
The `hole_puncher` is an adaptive, $O(1)$ quarantine structure that buffers out-of-order packets into a statically sized `ring_buffer_`. 

Instead of looping bit-by-bit or relying on complex trees, the `hole_puncher` tracks the `peak_jitter_` (the maximum observed sequence gap). If jitter exceeds safe thresholds, it automatically triggers a Jitter Recovery Policy, dropping the oldest missing packets (tombstoning) to force the pipeline to slide forward and prevent indefinite stalling.

## 3. Continuous Prefix Handoff
To process ready packets, the Sovereign Core invokes `flush_ready(Func&& processor)`. 

This method performs a contiguous scan starting from `expected_lsn_`. If slots are occupied and not marked as skipped, it feeds the payload directly into your provided lambda/functor. It then automatically executes `decay_reorder_window()` to shrink the effective buffering depth back down during stable network conditions.

### Hands-On: Hardware-Accelerated Reordering

```cpp
#include "slabflux/core/hole_puncher.hpp"
#include "slabflux/core/pipeline.hpp"
#include <iostream>

struct MeshPacket {
    uint64_t lsn;
    double data;
};

struct ReorderHandler {
    slabflux::core::hole_puncher<MeshPacket, 64> quarantine;
    uint64_t expected_lsn = 1;

    void on(const MeshPacket& packet) {
        // 1. O(1) Mask insertion
        quarantine.insert(packet.lsn, packet);
        
        // 2. Flush all contiguous ready packets
        quarantine.flush_ready([this](const MeshPacket& data, uint64_t sequence) {
            std::cout << "[FLUSH] Executing LSN: " << sequence << "\n";
            this->expected_lsn = sequence + 1;
        });
    }
};

int main() {
    ReorderHandler handler;
    slabflux::core::pipeline<ReorderHandler> pipe(handler);
    // ... dispatch out-of-order LSNs ...
}
```