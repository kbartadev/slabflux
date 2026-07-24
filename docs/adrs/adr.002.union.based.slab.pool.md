# ADR 002: Union-Based Slab Pool Memory Layout

## Status

Accepted


## Context

Lock-free free-lists require storing a next pointer (or index) inside unallocated memory blocks. Previously, SLABFLUX used `reinterpret_cast` to write the index directly into the first 4 bytes of the payload. This caused undefined behavior due to offset mismatching and strict aliasing violations.


## Decision

We enforce a `union` structure for all memory cells:
~~~cpp
union alignas(64) cell {
    unsigned char payload[sizeof(Event)];
    uint32_t next_index;
};
~~~

## Consequences

### Positives

- Complete elimination of `reinterpret_cast`
- Guaranteed C++ pointer interconvertibility (`&cell == &payload == &next_index`)
- Guaranteed minimum size of 4 bytes for empty events

### Negatives
None. The layout is strictly compliant with the C++20 standard.
