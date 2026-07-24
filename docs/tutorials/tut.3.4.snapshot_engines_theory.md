# Tutorial 08: Snapshot Engines & Sovereign State Recovery

## 1. The Geometry of Deterministic State

Because SlabFlux is strictly deterministic, replaying the exact sequence of historical events (the Causal Mesh) guarantees that the pipeline will arrive at the exact same physical memory state. 

However, in a high-availability cluster (e.g., active-passive failover in HFT), replaying millions of events from the start of the trading day takes too long. To achieve microsecond-level failover, we use **Snapshot Engines**.

A Snapshot Engine periodically captures the entire topological state of your `context_vault` and flushes it to disk without interrupting the Sovereign Core.

## 2. Asynchronous State Serialization

Standard serialization (e.g., locking a mutex and writing JSON/Protobuf) is strictly forbidden. 

Instead, we combine the Zero-Copy capabilities of our memory geometries with a Lock-Free Conduit:
1. The Sovereign Core performs a rapid `memcpy` of the raw Context Vault (which is $O(1)$ and cache-aligned) into a pre-allocated Snapshot Slot in an SPSC Conduit.
2. An isolated OS thread (the Snapshot Poller) detects the completed phase tag, compresses the payload, and submits it to disk via `io_uring`.

## 3. Hands-On: Building a Snapshot Trigger

```cpp
#include "slabflux/core/context.hpp"
#include <cstring>
#include <cstdint>

// 1. The Context Vault (Deterministic state)
struct alignas(64) TradingStateVault {
    uint64_t current_epoch;
    double exposure_limit;
    double active_positions[100];
};

// 2. The Snapshot Conduit Slot
struct alignas(64) snapshot_slot {
    TradingStateVault state_copy;
    uint64_t sequence_number;
    uint32_t padding[13];
    uint32_t phase_tag;
};

static_assert(sizeof(snapshot_slot) % 64 == 0, "Snapshot slot must align to cache geometry.");

// 3. The Execution Handler
struct MarketHandler {
    REGISTER_CONTEXT(TradingStateVault, vault);
    
    snapshot_slot* snap_conduit;
    uint32_t ring_size;
    uint32_t current_idx = 0;
    uint32_t current_phase = 1;
    uint64_t seq_num = 0;

    // The 'vault' reference is injected automatically by the pipeline
    void on(const MarketTick& e, TradingStateVault& vault) {
        // Process market tick, mutating vault...
        seq_num++;

        // Trigger a zero-blocking snapshot every 10,000 ticks
        if (seq_num % 10000 == 0) {
            // Deep physical copy of the vault (Fast due to aligned geometry)
            std::memcpy(&snap_conduit[current_idx].state_copy, &vault, sizeof(TradingStateVault));
            snap_conduit[current_idx].sequence_number = seq_num;

            // Release fence & phase publish
            __atomic_store_n(&snap_conduit[current_idx].phase_tag, current_phase, __ATOMIC_RELEASE);

            // Phase advance
            current_idx = (current_idx + 1) & (ring_size - 1);
            if (current_idx == 0) [[unlikely]] {
                current_phase = (current_phase + 1) & 0xFF;
                if (current_phase == 0) current_phase = 1;
            }
        }
    }
};
```

## 4. State Recovery on Failover

When the passive node becomes active:
1. The engine reads the latest snapshot from disk using `O_DIRECT`.
2. It maps the raw bytes directly over the passive node's Context Vault memory allocation (no deserialization logic needed).
3. The Ingress network poller requests replay of any packet sequence numbers greater than the snapshot's `sequence_number`.
4. The passive node is instantly caught up and assumes the Sovereign Core role.