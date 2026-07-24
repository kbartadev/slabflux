# Tutorial 14: The Network Gateway & Zero-Copy Ingress

When your engine communicates with the outside world via TCP or UDP, the standard `recv()` and `send()` POSIX calls create a bottleneck because they copy data from the kernel space into the user space. 

SLABFLUX provides the `gateway` pattern. Gateways sit at the boundary of your application, bypassing unnecessary copies by directly casting raw network buffer memory into your perfectly aligned C++ structures.

## 1. Zero-Copy Packet Demultiplexing

Instead of reading a byte array, parsing it into a JSON or `std::string`, and then building an object, SLABFLUX expects the incoming network packet to exactly match the memory layout of your `POD` event structures.

```cpp
#include "slabflux/core/pipeline.hpp"
#include "slabflux/net/demux_gateway.hpp"
#include <iostream>

// The memory layout of this struct MUST match the exact byte layout of the incoming UDP/TCP packet.
struct alignas(64) player_input_packet {
    uint16_t packet_id; // e.g., 0x01 for movement
    float pos_x;
    float pos_y;
};

struct input_handler {
    void on(const player_input_packet* packet) {
        if (!packet) return;
        std::cout << "Player moved to: X=" << packet->pos_x << " Y=" << packet->pos_y << "\n";
    }
};
```

## 2. Binding to the Chicago Gateway

The Chicago Gateway uses a `jump_table` (an array of function pointers). You register your event types at startup, and from then on, routing is just a single array index lookup—no branches, no `if-else` chains.

```cpp
#include "slabflux/core/pipeline.hpp"
#include "slabflux/net/demux_gateway.hpp"

struct input_handler {
    void on(const player_input_packet* packet) {
        std::cout << "Player moved to: " << packet->pos_x << "\n";
    }
};

int main() {
    input_handler handler;
    slabflux::core::pipeline pipe(handler);
    
    // Create the Gateway, tied to our pipeline
    slabflux::net::demux_gateway<decltype(pipe)> gateway(pipe);
    
    // Register the packet ID to the routing table
    gateway.bind<player_input_packet>();
    
    return 0;
}
```

## 3. The Zero-Copy Ingress

When bytes arrive, you don't parse. You just call `on_network_bytes_received`. The gateway internally fetches the function pointer from the jump table and dispatches the pointer to your logic pipeline immediately.

```cpp
void on_network_bytes_received(const char* raw_buffer, 
                               slabflux::net::demux_gateway<decltype(pipe)>& gateway,
                               slabflux::core::pipeline<input_handler>& pipe) {
    
    // O(1) jump table lookup:
    // The gateway automatically casts the raw buffer to the registered event type
    // and calls pipe.dispatch() in one contiguous flow.
    gateway.on_network_bytes_received(raw_buffer, pipe);
}
```
