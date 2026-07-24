## 2. The Gateway Router

The Gateway takes the raw network byte buffer, identifies the packet type, and perfectly forwards a pointer of that memory address directly into your pipeline. Zero allocations. Zero data copying.

```cpp
void on_network_bytes_received(const char* raw_buffer, slabflux::core::pipeline<input_handler>& pipe) {
    // Read the first 2 bytes to determine packet type (without copying the payload)
    uint16_t packet_id = *reinterpret_cast<const uint16_t*>(raw_buffer);
    
    if (packet_id == 0x01) {
        // Cast the raw network buffer directly into our strictly aligned struct.
        // We dispatch the pointer straight into the logic pipeline!
        auto* packet = reinterpret_cast<const player_input_packet*>(raw_buffer);
        pipe.dispatch(packet);
    }
}
```

## 1. Reconstructing State via `replay_saga`

If your system crashes, or if you need to debug a complex edge case that occurred in production, you simply boot an empty instance of your pipeline and stream the recorded events back into it.

```cpp
#include "slabflux/core/replay_saga.hpp"
#include "slabflux/core/pipeline.hpp"

// A stateful engine (e.g., an Order Book or a Game World)
struct world_state_engine {
    int active_players = 0;
    
    void on(const player_join_event& ev) {
        active_players++;
    }
    void on(const player_leave_event& ev) {
        active_players--;
    }
};

void restore_system_state(const std::vector<const char*>& journaled_network_packets) {
    world_state_engine engine;
    slabflux::core::pipeline<world_state_engine> pipe(engine);
    
    std::cout << "[Replay] Starting state reconstruction...\n";
    
    // Inject the historical events into the empty engine as fast as the CPU allows
    for (const char* raw_bytes : journaled_network_packets) {
        // Route the bytes through the gateway into the pipeline
        gateway_route_bytes(raw_bytes, pipe);
    }
    
    // The engine's state is now perfectly identical to what it was before the crash.
    std::cout << "[Replay] Reconstruction complete. Active players: " << engine.active_players << "\n";
}
```
