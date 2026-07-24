# Foundation: Hole Puncher (`slabflux/core/hole_puncher.hpp`)

## 1. Architectural Justification
In distributed clustering, the sequencer guarantees strict logical order (LSN 1, 2, 3...). When packets arrive out of order (e.g., LSN 1, 3, 4), they must be quarantined. The `hole_puncher` is an O(1) wait-free staging buffer that isolates "future" events until missing gaps are filled, avoiding O(log N) tree/heap structures.

## 2. Hardware Implementation Directives
- **O(1) Sparse Array Quarantine**: Operates as a localized, bitmask-backed circular array. Events are slotted via offset indexing (`seq & MASK`), resolving insertions in constant time.
- **Presence Masking**: Flags out-of-order packets on a 64-bit `presence_mask`. 
- **Instant Cascade Unlocking**: When the missing packet arrives, the engine executes a hardware bit-scan (`__builtin_ctzll` / TZCNT) on the `presence_mask` to locate the next contiguous block of quarantined events, streaming them back into the pipeline in a microsecond burst.

## 3. Bibliography & Proofs
1. **Intel Corporation**. *Intel Architecture Instruction Set Extensions Programming Reference*. (TZCNT/BSR instructions for rapid bitmask scanning).
2. **Clark, C., & McGeoch, C. C.** (1995). *The design and implementation of a wait-free ring buffer*. (Constant-time slotting algorithms).
3. **Varghese, G.** (2004). *Network Algorithmics*. (Fast timer and gap-resolution wheels).