# Tutorial 10: Distributed Determinism & Epoch Aliasing

## 1. The Distributed State Machine

When deploying SlabFlux across a Causal Mesh (multiple physical servers acting as a cluster), you must guarantee that Node A and Node B produce the exact same topological state in their Context Vaults.

If a UDP multicast packet containing market data drops on Node A but arrives on Node B, Node A's pipeline will diverge. If Node A processes a local timer event before an upstream network event, and Node B processes them in reverse order, the cluster is corrupted.

## 2. Logical Sequencing over Physical Time

Sovereign Cores cannot use `std::chrono::system_clock::now()` to drive logic. Wall-clock time on physical hardware experiences jitter and NTP drift. 

Instead, all external events (market data, client orders) must be routed through a **Sequencer Node**. The Sequencer stamps every packet with a monotonically increasing `Epoch Sequence Number` (the $V$-axis of the dispatcher).

## 3. Strict Reordering Ingress Buffer

When the SlabFlux Ingress threads receive packets, they do not push them to the Sovereign Core blindly. They use an $O(1)$ Reorder Buffer. 

The Sovereign Core enforces that it only executes event $N$. If event $N+1$ arrives, it is buffered. If event $N$ does not arrive within a microsecond window, the NIC transmits a NAK to the mesh to request a replay.

### Hands-On: Sequence Guarding

```cpp
#include "slabflux/core/pipeline.hpp"

struct SequencedEvent {
    uint64_t epoch_sequence;
};

// A Vault tracking the determinism boundary
struct alignas(64) EpochVault {
    uint64_t expected_sequence = 1;
};

struct EpochArbiter {
    REGISTER_CONTEXT(EpochVault, epoch);

    // This is a Guard-Before-Action handler. Executed First.
    bool on(SequencedEvent& e) {
        if (e.epoch_sequence > epoch().expected_sequence) {
            // Out of order: HALT pipeline. 
            // (Ingress layer handles putting it in the reorder buffer)
            return true; 
        }
        
        if (e.epoch_sequence < epoch().expected_sequence) {
            // Duplicate / Historical replay: HALT pipeline.
            return true; 
        }

        // Perfect match. Advance the topology's expected sequence.
        epoch().expected_sequence++;
        
        // Continue to physical execution
        return false;
    }
};
```

## 4. Conclusion

By projecting strict causal sequencing into the `EpochArbiter` at the very base of your execution topology, the entire SlabFlux dispatcher collapses into a perfectly replicated State Machine. No random variables, no unpredictable thread scheduling, and no wall-clock time can violate the $O(1)$ $N \times M$ execution manifold.