# SlabFlux Core: SPSC Ring Conduit (`spsc_ring_conduit.hpp`)

## 1. Architectural Overview
An evolution of the standard SPSC conduit that exposes distinct allocation and commitment phases, allowing true zero-copy, in-place payload construction across thread boundaries.

## 2. Two-Phase Commit
- **Reserve Phase**: Producers invoke `.reserve()` to claim a memory slot exclusively and construct the object directly within the ring buffer's memory span.
- **Commit Phase**: Producers invoke `.commit()` to publish the memory barrier to the consumer.

This eliminates intermediary stack object copies entirely, pushing data straight from the network parsing logic to the destination thread natively.