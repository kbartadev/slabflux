# Cross-Orthogonal Queue (`cross_orthogonal_queue.hpp`)

## 1. Cross-Orthogonal Grid
The `cross_orthogonal_queue` is a specialized MPMC (Multi-Producer Multi-Consumer) structure that applies the principles of orthogonal manifolds specifically for Message Passing purposes. Its primary goal is to avoid hotspots on shared memory addresses, which occur in the `fetch_add` ticket assignment mechanisms of traditional queues.

### Architectural Concept
A refined MPMC iteration of the `orthogonal_manifold` implementation, using a "Cross-Orthogonal" network. The starting position of the threads is determined by a seed calculated from hardware entropy (thread ID hash), so the cores automatically spread evenly across the memory space.

### Synchronization Model
- **ABA Safety:** `nullptr` -> `T*` and `T*` -> `nullptr` CAS operations ensure atomic handoff.
- No spin-wait. If CAS fails, the cursor moves linearly forward (O(1) step).

## 2. Deterministic Entropy-Dispersion
Threads (neither Producers nor Consumers) do not start scanning at the `(0,0)` point of the matrix.
- Upon entry, a fast hash function based on hardware entropy (Knuth Multiplicative Hash from `std::thread::id`) determines the thread's local starting row and column.
- This **Spatial Dispersion** guarantees that if 32 threads try to write to the queue in the same picosecond, the memory accesses will fall on 32 different physical memory addresses (and optimally different Cache Banks/Nodes), eliminating the "Thundering Herd" problem.

## 3. Spinless Progression
Traditional lock-free queues enclose the `compare_exchange_weak` instruction in a `while` loop: if the write fails, the thread retries over and over at the same memory address. Under extreme load, this leads to Thermal Throttling and a complete lockout of the memory bus.

The Cross-Orthogonal Queue discards this paradigm:
```cpp
if (grid_[p_row].slots[col].compare_exchange_strong(expected, payload, ...)) {
    return true; // Success
}
// No LOOP on FAILURE!
// p_col advances to the next cell.
```
Ha a CAS elbukik, a szál tudomásul veszi, hogy a cella már nem `VACUUM` (vagy valaki megnyerte a versenyt), és azonos CPU cikluson belül tovább ugrik a következő cellára. Ez az `O(1)` továbbhaladás adja a rendszer Wait-Free (korlátos lépésszámú) garanciáját a Producer oldalon.

## 4. Hardveres Skálázódás
A rendszer throughputja nem süllyed a szálak számának növekedésével (ellentétben a klasszikus LMAX Disruptor-al vagy Folly MPMC-vel, amik 8-16 szál felett degradálódnak az RFO invalidációk miatt). Ahogy nő a terhelés, a szálak egyre sűrűbben szóródnak szét a mátrix felületén, így a queue memóriabusz-szaturációja egyenletes marad.