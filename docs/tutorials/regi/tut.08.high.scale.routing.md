# Tutorial 8: Scaling (Fan-In and Fan-Out Routing)

When the system grows, the SPSC (Single-Producer Single-Consumer) model alone is no longer sufficient. You may need multiple network interfaces feeding data into a single engine (Fan-In), or a load balancer distributing work across multiple worker threads (Fan-Out).

Since MPSC (Multi-Producer) queues are far too slow for the framework due to locking, SLABFLUX provides dedicated deterministic routers.

## 1. Fan-In: The Round-Robin Poller

The `round_robin_poller` allows a single Consumer thread to read fairly and without waiting (no locks) from multiple independent SPSC channels. It is perfect for monitoring multiple network interfaces (NICs) simultaneously.

```cpp
#include "slabflux/bridge/round_robin_poller.hpp"

// Two independent incoming network channels
slabflux::core::spsc_conduit<network_packet*, 1024> eth0_bus;
slabflux::core::spsc_conduit<network_packet*, 1024> eth1_bus;

void logic_thread() {
    // The poller aggregates the two channels
    slabflux::bridge::round_robin_poller<network_packet*, 2> poller;
    poller.bind(0, eth0_bus);
    poller.bind(1, eth1_bus);

    while (true) {
        // Cycles through the channels at O(1) speed
        auto* packet = poller.poll();
        
        if (packet) {
            dispatch(packet);
            pool.release(packet);
        }
    }
}
```

## 2. Fan-Out: The Round-Robin Switch (Sharding)

The `round_robin_switch` is the equivalent of a load balancer. It distributes events from a single incoming source across multiple parallel SPSC channels. It is ideal for distributing players across different game server instances (Sharding).

```cpp
#include "slabflux/bridge/round_robin_switch.hpp"

// Three independent worker-thread channels
slabflux::core::spsc_conduit<network_packet*, 1024> shard1;
slabflux::core::spsc_conduit<network_packet*, 1024> shard2;
slabflux::core::spsc_conduit<network_packet*, 1024> shard3;

void ingress_thread() {
    slabflux::bridge::round_robin_switch<network_packet*, 3> router;
    router.bind(0, shard1);
    router.bind(1, shard2);
    router.bind(2, shard3);

    while (true) {
        auto* packet = receive_from_network();
        
        // The router automatically finds the next available channel.
        // If all channels are full, it returns false.
        if (!router.route(packet)) {
            // Overload! Drop the packet.
            pool.release(packet);
        }
    }
}
```
