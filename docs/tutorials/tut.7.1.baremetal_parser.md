# Tutorial 7.1: Zero-Copy Protocol Parsing

## 1. The Deserialization Bottleneck
In high-frequency environments, copying network bytes into user-space structs using standard libraries (`std::memcpy`, `ntohl`) introduces branching and latency. The `transport/` subsystem in SlabFlux handles this directly off the wire.

## 2. The `baremetal_parser.hpp` Module
The `baremetal_parser` is responsible for translating raw physical network buffers into deterministic execution events. 

*Note: The exact wire protocols (e.g., FIX, ITCH, SBE) supported by `baremetal_parser` are Unknown — not inferable from the provided codebase.*

Architecturally, the parser pairs directly with `network_conduit.hpp`. Instead of allocating a new event, the `baremetal_parser` maps a zero-overhead `static_cast` view or utilizes AVX-accelerated intrinsic operations to parse the protocol headers in-place.

## 3. Structural Integration
The `baremetal_parser` typically sits as the first handler in your `pipeline`, acting as the Guard-Before-Action node.

```cpp
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/net/network_conduit.hpp"
#include "slabflux/core/pipeline.hpp"

struct ParserHandler {
    // Receives raw zero-copy buffer from the conduit
    void on(const NetworkPacket& raw_packet) {
        // Evaluates the physical bytes. 
        // Exact baremetal_parser API is Unknown — not inferable from the provided codebase.
        // slabflux::transport::baremetal_parser::parse(raw_packet.buffer);
    }
};

int main() {
    ParserHandler parser;
    slabflux::core::pipeline<ParserHandler> pipe(parser);
    
    // Pass to Sovereign Core...
}
```