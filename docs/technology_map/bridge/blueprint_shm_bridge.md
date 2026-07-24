# Blueprint: shm_bridge.hpp

## Architectural Overview
Provides cross-process boundary bridging utilizing POSIX Shared Memory segments, acting as a wait-free SPSC queue spanning independent OS processes.

## Core Logic & Mechanisms
- **Matrix Mapping (`shm_open`)**: Maps named `/dev/shm` files to memory addresses accessible by distinct processes natively.
- **Sovereign Geometry**: Separates the Control Block (atomic indices) from the Payload Block by exact cache-line dimensions to prevent false sharing between Process A and Process B.
- **Creator / Joiner Roles**: Enforces strict initialization barriers, ensuring that the joiner process spin-waits until the creator process asserts the synchronization magic bytes (`0xCAFEBABE`).